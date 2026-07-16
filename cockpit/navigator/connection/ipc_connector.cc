#include "cockpit/navigator/connection/ipc_connector.h"

#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>

namespace cockpit {
namespace navigator {
namespace {

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

bool WriteAll(int fd, const std::string& value) {
  std::size_t offset = 0;
  while (offset < value.size()) {
    const ssize_t written = send(fd, value.data() + offset, value.size() - offset, MSG_NOSIGNAL);
    if (written < 0 && errno == EINTR) {
      continue;
    }
    if (written <= 0) {
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
      *error = std::string("failed to read Unix socket: ") + std::strerror(errno);
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
  if (bind(socket_fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0 ||
      listen(socket_fd_, 16) < 0) {
    *error = std::string("failed to bind Unix socket ") + socket_path + ": " + std::strerror(errno);
    Close();
    return false;
  }
  socket_path_ = socket_path;
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
  pollfd client{client_fd, POLLIN, 0};
  if (poll(&client, 1, 1000) <= 0 || (client.revents & POLLIN) == 0) {
    close(client_fd);
    return -1;
  }
  char buffer[4096];
  const ssize_t size = read(client_fd, buffer, sizeof(buffer));
  if (size <= 0) {
    close(client_fd);
    return -1;
  }
  request->assign(buffer, static_cast<std::size_t>(size));
  const std::size_t newline = request->find('\n');
  if (newline != std::string::npos) {
    request->resize(newline);
  }
  return client_fd;
}

void IpcConnector::ReplyAndClose(int client_fd, const std::string& response) const {
  WriteAll(client_fd, response);
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
    if (lstat(socket_path_.c_str(), &existing) == 0 && S_ISSOCK(existing.st_mode)) {
      unlink(socket_path_.c_str());
    }
    socket_path_.clear();
  }
}

bool IpcConnector::SendRequest(const std::string& socket_path, const std::string& request,
                               std::string* response, std::string* error) {
  response->clear();
  error->clear();
  sockaddr_un address;
  if (!FillAddress(socket_path, &address, error)) {
    return false;
  }
  const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    *error = std::string("failed to create Unix socket: ") + std::strerror(errno);
    return false;
  }
  if (connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
    *error = std::string("failed to connect to ") + socket_path + ": " + std::strerror(errno);
    close(fd);
    return false;
  }
  if (!WriteAll(fd, request + "\n")) {
    *error = std::string("failed to write Unix socket: ") + std::strerror(errno);
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
