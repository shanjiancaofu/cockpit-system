#include "common/can/socket_can.h"

#if !defined(__linux__)
#error "SocketCan requires Linux"
#endif

#include <cerrno>
#include <cstring>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <utility>

namespace cockpit {
namespace can {
namespace {

std::string SystemError(const std::string& action) {
  return action + ": " + std::strerror(errno);
}

}  // namespace

SocketCan::~SocketCan() {
  Close();
}

SocketCan::SocketCan(SocketCan&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}

SocketCan& SocketCan::operator=(SocketCan&& other) noexcept {
  if (this != &other) {
    Close();
    fd_ = std::exchange(other.fd_, -1);
  }
  return *this;
}

bool SocketCan::Open(const std::string& interface_name, std::string* error) {
  Close();
  if (interface_name.empty() || interface_name.size() >= IFNAMSIZ) {
    SetError(error, "invalid CAN interface name: " + interface_name);
    return false;
  }

  const int socket_fd = ::socket(PF_CAN, SOCK_RAW | SOCK_CLOEXEC, CAN_RAW);
  if (socket_fd < 0) {
    SetError(error, SystemError("create CAN socket failed"));
    return false;
  }

  struct ifreq request {};
  std::memcpy(request.ifr_name, interface_name.c_str(), interface_name.size() + 1);
  if (::ioctl(socket_fd, SIOCGIFINDEX, &request) < 0) {
    SetError(error, SystemError("resolve CAN interface failed"));
    ::close(socket_fd);
    return false;
  }

  struct sockaddr_can address {};
  address.can_family = AF_CAN;
  address.can_ifindex = request.ifr_ifindex;
  if (::bind(socket_fd, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) < 0) {
    SetError(error, SystemError("bind CAN socket failed"));
    ::close(socket_fd);
    return false;
  }

  fd_ = socket_fd;
  return true;
}

void SocketCan::Close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

bool SocketCan::Send(const CanFrame& frame, std::string* error) const {
  if (!IsOpen()) {
    SetError(error, "CAN socket is closed");
    return false;
  }
  if (!frame.IsValid()) {
    SetError(error, "invalid CAN frame");
    return false;
  }

  struct can_frame native_frame {};
  native_frame.can_id = frame.id();
  if (frame.extended()) {
    native_frame.can_id |= CAN_EFF_FLAG;
  }
  if (frame.remote()) {
    native_frame.can_id |= CAN_RTR_FLAG;
  }
  native_frame.can_dlc = frame.data_length();
  std::memcpy(native_frame.data, frame.data().data(), frame.data_length());

  ssize_t written = -1;
  do {
    written = ::write(fd_, &native_frame, sizeof(native_frame));
  } while (written < 0 && errno == EINTR);

  if (written != static_cast<ssize_t>(sizeof(native_frame))) {
    SetError(error, SystemError("send CAN frame failed"));
    return false;
  }
  return true;
}

CanIoStatus SocketCan::Receive(CanFrame* frame, int timeout_ms, std::string* error) const {
  if (!IsOpen()) {
    SetError(error, "CAN socket is closed");
    return CanIoStatus::kClosed;
  }
  if (frame == nullptr || timeout_ms < -1) {
    SetError(error, "invalid CAN receive arguments");
    return CanIoStatus::kError;
  }

  struct pollfd descriptor {};
  descriptor.fd = fd_;
  descriptor.events = POLLIN;

  int poll_result = -1;
  do {
    poll_result = ::poll(&descriptor, 1, timeout_ms);
  } while (poll_result < 0 && errno == EINTR);

  if (poll_result == 0) {
    return CanIoStatus::kTimeout;
  }
  if (poll_result < 0) {
    SetError(error, SystemError("poll CAN socket failed"));
    return CanIoStatus::kError;
  }
  if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
    SetError(error, "CAN socket reported an invalid poll state");
    return CanIoStatus::kError;
  }

  struct can_frame native_frame {};
  ssize_t received = -1;
  do {
    received = ::read(fd_, &native_frame, sizeof(native_frame));
  } while (received < 0 && errno == EINTR);

  if (received != static_cast<ssize_t>(sizeof(native_frame))) {
    SetError(error, SystemError("receive CAN frame failed"));
    return CanIoStatus::kError;
  }

  const bool extended = (native_frame.can_id & CAN_EFF_FLAG) != 0;
  const bool remote = (native_frame.can_id & CAN_RTR_FLAG) != 0;
  const std::uint32_t id = native_frame.can_id & (extended ? CAN_EFF_MASK : CAN_SFF_MASK);
  std::array<std::uint8_t, CanFrame::kMaxDataLength> data{};
  const std::uint8_t data_length =
      native_frame.can_dlc > CanFrame::kMaxDataLength
          ? static_cast<std::uint8_t>(CanFrame::kMaxDataLength)
          : native_frame.can_dlc;
  std::memcpy(data.data(), native_frame.data, data_length);
  *frame = CanFrame(id, data, data_length, extended, remote);
  return CanIoStatus::kOk;
}

void SocketCan::SetError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

}  // namespace can
}  // namespace cockpit
