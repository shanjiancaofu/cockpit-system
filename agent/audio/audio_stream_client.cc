#include "agent/audio/audio_stream_client.h"

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "cockpit/modules/audio/transport/audio_stream_protocol.h"

namespace cockpit {
namespace agent {
namespace {

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

}  // namespace

AudioStreamClient::~AudioStreamClient() {
  Close();
}

bool AudioStreamClient::Connect(const std::string& socket_path, std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  Close();
  sockaddr_un address{};
  if (!FillAddress(socket_path, &address, error)) {
    return false;
  }
  socket_fd_ = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
  if (socket_fd_ < 0) {
    SetError(error, std::string("failed to create audio stream socket: ") + std::strerror(errno));
    return false;
  }
  if (connect(socket_fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
    SetError(error, "failed to connect to audio stream " + socket_path + ": " +
                        std::string(std::strerror(errno)));
    Close();
    return false;
  }
  return true;
}

AudioStreamReceiveResult AudioStreamClient::ReceiveFrame(int timeout_ms) {
  AudioStreamReceiveResult result;
  if (socket_fd_ < 0) {
    result.error = "audio stream is not connected";
    return result;
  }
  if (timeout_ms < 0) {
    result.error = "audio stream timeout must not be negative";
    return result;
  }

  pollfd descriptor{socket_fd_, POLLIN, 0};
  int ready = 0;
  do {
    ready = poll(&descriptor, 1, timeout_ms);
  } while (ready < 0 && errno == EINTR);
  if (ready == 0) {
    result.status = AudioStreamReceiveStatus::kTimeout;
    return result;
  }
  if (ready < 0) {
    result.error = "failed while waiting for audio frame: " + std::string(std::strerror(errno));
    return result;
  }
  if ((descriptor.revents & (POLLHUP | POLLERR | POLLNVAL)) != 0 &&
      (descriptor.revents & POLLIN) == 0) {
    result.status = AudioStreamReceiveStatus::kDisconnected;
    Close();
    return result;
  }

  audio::AudioStreamCapturePacket packet{};
  ssize_t received = 0;
  do {
    received = recv(socket_fd_, packet.data(), packet.size(), MSG_TRUNC);
  } while (received < 0 && errno == EINTR);
  if (received == 0) {
    result.status = AudioStreamReceiveStatus::kDisconnected;
    Close();
    return result;
  }
  if (received < 0) {
    result.error = "failed to read audio frame: " + std::string(std::strerror(errno));
    return result;
  }

  auto frame = audio::DecodeAudioStreamCaptureFrame(
      packet.data(), static_cast<std::size_t>(received), &result.error);
  if (!frame.has_value()) {
    return result;
  }
  result.frame.emplace(std::move(*frame));
  result.status = AudioStreamReceiveStatus::kFrame;
  return result;
}

void AudioStreamClient::Close() {
  if (socket_fd_ >= 0) {
    close(socket_fd_);
    socket_fd_ = -1;
  }
}

}  // namespace agent
}  // namespace cockpit
