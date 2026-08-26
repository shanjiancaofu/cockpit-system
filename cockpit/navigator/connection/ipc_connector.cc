#include "cockpit/navigator/connection/ipc_connector.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>

namespace cockpit {
namespace navigator {
namespace {

constexpr int kIoTimeoutMs = 1000;
constexpr std::size_t kMaximumRequestBytes = std::size_t{64} * 1024U;

bool FillAddress(const std::string& path, sockaddr_un* address, std::string* error) {
  if (path.empty() || path.size() >= sizeof(address->sun_path)) {
    *error = "Unix socket path is empty or too long: " + path;
    return false;
  }
  std::memset(address, 0, sizeof(*address));
  address->sun_family = AF_UNIX;
  std::memcpy(address->sun_path, path.c_str(), path.size() + 1);
  return true;
}

bool WriteAll(int fd, const std::string& value, std::string* error) {
  std::size_t offset = 0;
  while (offset < value.size()) {
    const ssize_t written = send(fd, value.data() + offset, value.size() - offset, MSG_NOSIGNAL);
    if (written < 0 && errno == EINTR) {
      continue;
    }
    if (written < 0) {
      if (error != nullptr) {
        *error = errno == EAGAIN || errno == EWOULDBLOCK
                     ? "timed out writing Unix socket request"
                     : std::string("failed to write Unix socket: ") + std::strerror(errno);
      }
      return false;
    }
    if (written == 0) {
      if (error != nullptr) {
        *error = "Unix socket closed while writing request";
      }
      return false;
    }
    offset += static_cast<std::size_t>(written);
  }
  return true;
}

bool ReadToEnd(int fd, std::string* value, std::string* error) {
  char buffer[1024];
  while (true) {
    const ssize_t size = read(fd, buffer, sizeof(buffer));
    if (size < 0 && errno == EINTR) {
      continue;
    }
    if (size < 0) {
      *error = errno == EAGAIN || errno == EWOULDBLOCK
                   ? "timed out waiting for Unix socket response"
                   : std::string("failed to read Unix socket: ") + std::strerror(errno);
      return false;
    }
    if (size == 0) {
      return true;
    }
    value->append(buffer, static_cast<std::size_t>(size));
    if (value->size() > std::size_t{64} * 1024U) {
      *error = "Unix socket response exceeds 64 KiB";
      return false;
    }
  }
}

bool ExistingSocketIsActive(const sockaddr_un& address, std::string* error) {
  const int probe_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (probe_fd < 0) {
    *error = std::string("failed to create Unix socket probe: ") + std::strerror(errno);
    return true;
  }
  if (connect(probe_fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0) {
    close(probe_fd);
    *error = "Unix socket is already owned by a running Navigator";
    return true;
  }
  const int connect_error = errno;
  close(probe_fd);
  if (connect_error == ECONNREFUSED || connect_error == ENOENT) {
    return false;
  }
  *error = std::string("failed to probe existing Unix socket: ") + std::strerror(connect_error);
  return true;
}

}  // namespace

IpcConnector::~IpcConnector() {
  Close();
}

bool IpcConnector::Open(const std::string& socket_path, std::string* error) {
  Close();
  sockaddr_un address;
  if (!FillAddress(socket_path, &address, error)) {
    return false;
  }

  const std::string lock_path = socket_path + ".lock";
  lock_fd_ = open(lock_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
  if (lock_fd_ < 0) {
    *error = "failed to open Navigator lock " + lock_path + ": " + std::strerror(errno);
    return false;
  }
  if (flock(lock_fd_, LOCK_EX | LOCK_NB) < 0) {
    *error = errno == EWOULDBLOCK ? "another Navigator already owns " + socket_path
                                  : "failed to lock " + lock_path + ": " + std::strerror(errno);
    Close();
    return false;
  }

  socket_fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (socket_fd_ < 0) {
    *error = std::string("failed to create Unix socket: ") + std::strerror(errno);
    return false;
  }
  struct stat existing {};
  if (lstat(socket_path.c_str(), &existing) == 0) {
    if (!S_ISSOCK(existing.st_mode)) {
      *error = "refusing to replace non-socket path: " + socket_path;
      Close();
      return false;
    }
    if (ExistingSocketIsActive(address, error)) {
      Close();
      return false;
    }
    if (unlink(socket_path.c_str()) < 0) {
      *error = "failed to remove stale Unix socket " + socket_path + ": " + std::strerror(errno);
      Close();
      return false;
    }
  } else if (errno != ENOENT) {
    *error = "failed to inspect Unix socket path " + socket_path + ": " + std::strerror(errno);
    Close();
    return false;
  }
  if (bind(socket_fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
    *error = std::string("failed to bind Unix socket ") + socket_path + ": " + std::strerror(errno);
    Close();
    return false;
  }
  socket_path_ = socket_path;
  struct stat created {};
  if (lstat(socket_path.c_str(), &created) < 0 || !S_ISSOCK(created.st_mode)) {
    *error = "failed to inspect created Unix socket " + socket_path + ": " + std::strerror(errno);
    Close();
    return false;
  }
  socket_device_ = static_cast<std::uint64_t>(created.st_dev);
  socket_inode_ = static_cast<std::uint64_t>(created.st_ino);
  if (listen(socket_fd_, 16) < 0) {
    *error = "failed to listen on Unix socket " + socket_path + ": " + std::strerror(errno);
    Close();
    return false;
  }
  return true;
}

int IpcConnector::WaitForRequest(int timeout_ms, std::string* request) {
  pollfd descriptor{socket_fd_, POLLIN, 0};
  const int ready = poll(&descriptor, 1, timeout_ms);
  if (ready <= 0 || (descriptor.revents & POLLIN) == 0) {
    return -1;
  }

  const int client_fd = accept4(socket_fd_, nullptr, nullptr, SOCK_CLOEXEC);
  if (client_fd < 0) {
    return -1;
  }
  const timeval send_timeout{kIoTimeoutMs / 1000,
                             static_cast<suseconds_t>(kIoTimeoutMs % 1000) * 1000L};
  if (setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout)) < 0) {
    close(client_fd);
    return -1;
  }
  request->clear();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kIoTimeoutMs);
  while (true) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    if (remaining.count() <= 0) {
      close(client_fd);
      return -1;
    }
    pollfd client{client_fd, POLLIN, 0};
    const int client_ready = poll(&client, 1, static_cast<int>(remaining.count()));
    if (client_ready < 0 && errno == EINTR) {
      continue;
    }
    if (client_ready <= 0 || (client.revents & (POLLIN | POLLHUP)) == 0) {
      close(client_fd);
      return -1;
    }
    char buffer[1024];
    const ssize_t size = read(client_fd, buffer, sizeof(buffer));
    if (size < 0 && errno == EINTR) {
      continue;
    }
    if (size <= 0) {
      close(client_fd);
      return -1;
    }
    request->append(buffer, static_cast<std::size_t>(size));
    const std::size_t newline = request->find('\n');
    if (newline != std::string::npos) {
      if (newline > kMaximumRequestBytes) {
        ReplyAndClose(client_fd, "ERROR request exceeds 64 KiB\n");
        return -1;
      }
      request->resize(newline);
      return client_fd;
    }
    if (request->size() > kMaximumRequestBytes) {
      ReplyAndClose(client_fd, "ERROR request exceeds 64 KiB\n");
      return -1;
    }
  }
}

void IpcConnector::ReplyAndClose(int client_fd, const std::string& response) const {
  WriteAll(client_fd, response, nullptr);
  shutdown(client_fd, SHUT_WR);
  close(client_fd);
}

void IpcConnector::Close() {
  if (socket_fd_ >= 0) {
    close(socket_fd_);
    socket_fd_ = -1;
  }
  if (!socket_path_.empty()) {
    struct stat existing {};
    if (lstat(socket_path_.c_str(), &existing) == 0 && S_ISSOCK(existing.st_mode) &&
        static_cast<std::uint64_t>(existing.st_dev) == socket_device_ &&
        static_cast<std::uint64_t>(existing.st_ino) == socket_inode_) {
      unlink(socket_path_.c_str());
    }
    socket_path_.clear();
  }
  socket_device_ = 0;
  socket_inode_ = 0;
  if (lock_fd_ >= 0) {
    close(lock_fd_);
    lock_fd_ = -1;
  }
}

bool IpcConnector::SendRequest(const std::string& socket_path, const std::string& request,
                               std::string* response, std::string* error, int response_timeout_ms) {
  response->clear();
  error->clear();
  sockaddr_un address;
  if (!FillAddress(socket_path, &address, error)) {
    return false;
  }
  const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (fd < 0) {
    *error = std::string("failed to create Unix socket: ") + std::strerror(errno);
    return false;
  }
  const int connect_result =
      connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
  if (connect_result < 0) {
    if (errno != EINPROGRESS && errno != EAGAIN && errno != EINTR) {
      *error = std::string("failed to connect to ") + socket_path + ": " + std::strerror(errno);
      close(fd);
      return false;
    }
    pollfd descriptor{fd, POLLOUT, 0};
    const int ready = poll(&descriptor, 1, kIoTimeoutMs);
    if (ready == 0) {
      *error = "timed out connecting to " + socket_path;
      close(fd);
      return false;
    }
    if (ready < 0) {
      *error =
          std::string("failed while connecting to ") + socket_path + ": " + std::strerror(errno);
      close(fd);
      return false;
    }
    int socket_error = 0;
    socklen_t error_size = sizeof(socket_error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &error_size) < 0 || socket_error != 0) {
      const int connect_error = socket_error == 0 ? errno : socket_error;
      *error =
          std::string("failed to connect to ") + socket_path + ": " + std::strerror(connect_error);
      close(fd);
      return false;
    }
  }

  const int flags = fcntl(fd, F_GETFL, 0);
  const timeval send_timeout{kIoTimeoutMs / 1000,
                             static_cast<suseconds_t>(kIoTimeoutMs % 1000) * 1000L};
  const timeval receive_timeout{response_timeout_ms / 1000,
                                static_cast<suseconds_t>(response_timeout_ms % 1000) * 1000L};
  if (flags < 0 || fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) < 0 ||
      setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout)) < 0 ||
      setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout, sizeof(receive_timeout)) < 0) {
    *error = std::string("failed to configure Unix socket timeout: ") + std::strerror(errno);
    close(fd);
    return false;
  }
  if (!WriteAll(fd, request + "\n", error)) {
    close(fd);
    return false;
  }
  shutdown(fd, SHUT_WR);
  const bool read_ok = ReadToEnd(fd, response, error);
  close(fd);
  return read_ok;
}

}  // namespace navigator
}  // namespace cockpit
