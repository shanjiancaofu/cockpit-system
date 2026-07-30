#include "cockpit/library/driver/audio/transport/audio_stream_publisher.h"

#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <optional>
#include <utility>

#include "cockpit/modules/audio/transport/audio_stream_protocol.h"

namespace cockpit {
namespace audio {
namespace {

constexpr int kPollTimeoutMs = 50;
constexpr int kSendTimeoutMs = 20;

void SetError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

bool FillAddress(const std::string& path, sockaddr_un* address, std::string* error) {
  if (path.empty() || path.front() != '/') {
    SetError(error, "audio stream socket path must be absolute");
    return false;
  }
  if (path.size() >= sizeof(address->sun_path)) {
    SetError(error, "audio stream socket path is too long");
    return false;
  }
  std::memset(address, 0, sizeof(*address));
  address->sun_family = AF_UNIX;
  std::memcpy(address->sun_path, path.c_str(), path.size() + 1U);
  return true;
}

bool ExistingSocketIsActive(const sockaddr_un& address) {
  const int probe_fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
  if (probe_fd < 0) {
    return true;
  }
  const bool active =
      connect(probe_fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
  const int connect_error = errno;
  close(probe_fd);
  return active || (connect_error != ECONNREFUSED && connect_error != ENOENT);
}

}  // namespace

AudioStreamPublisher::AudioStreamPublisher(std::size_t queue_capacity)
    : queue_capacity_(queue_capacity) {
}

AudioStreamPublisher::~AudioStreamPublisher() {
  Stop();
}

bool AudioStreamPublisher::Start(const std::string& socket_path, std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  if (queue_capacity_ == 0) {
    SetError(error, "audio stream queue capacity must be positive");
    return false;
  }
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true)) {
    SetError(error, "audio stream publisher is already running");
    return false;
  }

  sockaddr_un address{};
  if (!FillAddress(socket_path, &address, error)) {
    running_.store(false);
    return false;
  }
  listener_fd_ = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (listener_fd_ < 0) {
    SetError(error, std::string("failed to create audio stream socket: ") + std::strerror(errno));
    running_.store(false);
    return false;
  }

  struct stat existing {};
  if (lstat(socket_path.c_str(), &existing) == 0) {
    if (!S_ISSOCK(existing.st_mode)) {
      SetError(error, "refusing to replace non-socket audio stream path: " + socket_path);
      Stop();
      return false;
    }
    if (ExistingSocketIsActive(address)) {
      SetError(error, "audio stream socket is already active: " + socket_path);
      Stop();
      return false;
    }
    if (unlink(socket_path.c_str()) < 0) {
      SetError(error,
               "failed to remove stale audio stream socket: " + std::string(std::strerror(errno)));
      Stop();
      return false;
    }
  } else if (errno != ENOENT) {
    SetError(error, "failed to inspect audio stream socket: " + std::string(std::strerror(errno)));
    Stop();
    return false;
  }

  if (bind(listener_fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
    SetError(error, "failed to open audio stream socket " + socket_path + ": " +
                        std::string(std::strerror(errno)));
    Stop();
    return false;
  }
  socket_path_ = socket_path;
  struct stat created {};
  if (lstat(socket_path.c_str(), &created) < 0 || !S_ISSOCK(created.st_mode)) {
    SetError(error, "failed to inspect created audio stream socket");
    Stop();
    return false;
  }
  socket_device_ = static_cast<std::uint64_t>(created.st_dev);
  socket_inode_ = static_cast<std::uint64_t>(created.st_ino);
  if (chmod(socket_path.c_str(), 0600) < 0 || listen(listener_fd_, 1) < 0) {
    SetError(error, "failed to open audio stream socket " + socket_path + ": " +
                        std::string(std::strerror(errno)));
    Stop();
    return false;
  }
  worker_ = std::thread(&AudioStreamPublisher::Run, this);
  return true;
}

void AudioStreamPublisher::Stop() {
  running_.store(false);
  frame_available_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
  if (listener_fd_ >= 0) {
    close(listener_fd_);
    listener_fd_ = -1;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    frames_.clear();
    discontinuity_pending_ = false;
  }
  RemoveOwnedSocket();
}

bool AudioStreamPublisher::Publish(const AudioFrame& frame) {
  if (!running_.load()) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (frames_.size() >= queue_capacity_) {
      frames_.pop_front();
      discontinuity_pending_ = true;
      frames_dropped_.fetch_add(1U);
    }
    frames_.push_back(frame);
    frames_queued_.fetch_add(1U);
  }
  frame_available_.notify_one();
  return true;
}

AudioStreamPublisherMetrics AudioStreamPublisher::metrics() const {
  AudioStreamPublisherMetrics result;
  result.clients_accepted = clients_accepted_.load();
  result.frames_queued = frames_queued_.load();
  result.frames_sent = frames_sent_.load();
  result.frames_dropped = frames_dropped_.load();
  result.client_disconnects = client_disconnects_.load();
  return result;
}

void AudioStreamPublisher::Run() {
  int client_fd = -1;
  while (running_.load()) {
    if (client_fd < 0) {
      client_fd = AcceptClient(listener_fd_);
      continue;
    }

    std::optional<AudioFrame> frame;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      frame_available_.wait_for(lock, std::chrono::milliseconds(kPollTimeoutMs), [this] {
        return !running_.load() || !frames_.empty();
      });
      if (!running_.load()) {
        break;
      }
      if (!frames_.empty()) {
        const AudioFrame& queued = frames_.front();
        AudioFrameFlag flags = queued.flags();
        if (discontinuity_pending_) {
          flags = flags | AudioFrameFlag::kDiscontinuity | AudioFrameFlag::kDroppedBefore;
          discontinuity_pending_ = false;
        }
        frame.emplace(queued.sequence(), queued.capture_time_ns(), flags, queued.samples());
        frames_.pop_front();
      }
    }
    if (frame.has_value() && !SendFrame(client_fd, *frame)) {
      close(client_fd);
      client_fd = -1;
      client_disconnects_.fetch_add(1U);
    }
  }
  if (client_fd >= 0) {
    close(client_fd);
  }
}

int AudioStreamPublisher::AcceptClient(int listener_fd) {
  pollfd descriptor{listener_fd, POLLIN, 0};
  const int ready = poll(&descriptor, 1, kPollTimeoutMs);
  if (ready <= 0 || (descriptor.revents & POLLIN) == 0) {
    return -1;
  }
  const int client_fd = accept4(listener_fd, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
  if (client_fd < 0) {
    return -1;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    discontinuity_pending_ = true;
  }
  clients_accepted_.fetch_add(1U);
  return client_fd;
}

bool AudioStreamPublisher::SendFrame(int client_fd, const AudioFrame& frame) {
  const AudioStreamCapturePacket packet = EncodeAudioStreamCaptureFrame(frame);
  ssize_t sent = send(client_fd, packet.data(), packet.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
  if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    pollfd descriptor{client_fd, POLLOUT, 0};
    const int ready = poll(&descriptor, 1, kSendTimeoutMs);
    if (ready > 0 && (descriptor.revents & POLLOUT) != 0) {
      sent = send(client_fd, packet.data(), packet.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
    }
  }
  if (sent != static_cast<ssize_t>(packet.size())) {
    return false;
  }
  frames_sent_.fetch_add(1U);
  return true;
}

void AudioStreamPublisher::RemoveOwnedSocket() {
  if (!socket_path_.empty()) {
    struct stat existing {};
    if (lstat(socket_path_.c_str(), &existing) == 0 && S_ISSOCK(existing.st_mode) &&
        static_cast<std::uint64_t>(existing.st_dev) == socket_device_ &&
        static_cast<std::uint64_t>(existing.st_ino) == socket_inode_) {
      unlink(socket_path_.c_str());
    }
  }
  socket_path_.clear();
  socket_device_ = 0;
  socket_inode_ = 0;
}

}  // namespace audio
}  // namespace cockpit
