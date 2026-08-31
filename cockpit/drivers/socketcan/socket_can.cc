#include "cockpit/drivers/socketcan/socket_can.h"

#include <linux/can.h>
#include <linux/can/error.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <limits>
#include <utility>

namespace cockpit {
namespace can {
namespace {

std::string SystemError(const std::string& action) {
  return action + ": " + std::strerror(errno);
}

bool TimespecNanoseconds(const struct timespec& timestamp, std::int64_t* nanoseconds) {
  constexpr std::int64_t kNanosecondsPerSecond = 1000000000LL;
  if (nanoseconds == nullptr || timestamp.tv_sec < 0 || timestamp.tv_nsec < 0 ||
      timestamp.tv_nsec >= kNanosecondsPerSecond ||
      timestamp.tv_sec > std::numeric_limits<std::int64_t>::max() / kNanosecondsPerSecond) {
    return false;
  }
  const auto seconds_ns = static_cast<std::int64_t>(timestamp.tv_sec) * kNanosecondsPerSecond;
  if (timestamp.tv_nsec > std::numeric_limits<std::int64_t>::max() - seconds_ns) return false;
  *nanoseconds = seconds_ns + timestamp.tv_nsec;
  return true;
}

}  // namespace

bool MapKernelRealtimeToSteady(std::int64_t kernel_realtime_ns, std::int64_t sampled_realtime_ns,
                               std::int64_t sampled_steady_ns, std::int64_t* received_steady_ns) {
  if (received_steady_ns == nullptr || kernel_realtime_ns <= 0 || sampled_realtime_ns <= 0 ||
      sampled_steady_ns <= 0 || kernel_realtime_ns > sampled_realtime_ns) {
    return false;
  }
  const std::int64_t age_ns = sampled_realtime_ns - kernel_realtime_ns;
  if (age_ns > sampled_steady_ns) return false;
  *received_steady_ns = sampled_steady_ns - age_ns;
  return true;
}

bool SocketCanFrame::IsValidForSend() const {
  const std::uint32_t id_mask = extended ? CAN_EFF_MASK : CAN_SFF_MASK;
  if ((id & ~id_mask) != 0U || length > kMaxDataLength || error) {
    return false;
  }
  if (fd) {
    return !remote;
  }
  return length <= CAN_MAX_DLEN && !brs && !esi;
}

bool SocketCanFrame::MapToLogicalSteadyMilliseconds(std::int64_t logical_dequeue_ms,
                                                    std::int64_t* received_steady_ms) const {
  constexpr std::int64_t kNanosecondsPerMillisecond = 1000000LL;
  if (!kernel_timestamp_valid || received_steady_ms == nullptr || logical_dequeue_ms < 0 ||
      received_steady_ns <= 0 || dequeued_steady_ns < received_steady_ns) {
    return false;
  }
  const std::int64_t age_ns = dequeued_steady_ns - received_steady_ns;
  const std::int64_t age_ms =
      age_ns / kNanosecondsPerMillisecond + (age_ns % kNanosecondsPerMillisecond != 0 ? 1 : 0);
  if (age_ms > logical_dequeue_ms) return false;
  *received_steady_ms = logical_dequeue_ms - age_ms;
  return true;
}

SocketCan::~SocketCan() {
  Close();
}

SocketCan::SocketCan(SocketCan&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {
}

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

  const int enable_fd = 1;
  if (::setsockopt(socket_fd, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable_fd, sizeof(enable_fd)) < 0) {
    SetError(error, SystemError("enable CAN FD frames failed"));
    ::close(socket_fd);
    return false;
  }
  const can_err_mask_t error_filter = CAN_ERR_MASK;
  if (::setsockopt(socket_fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &error_filter,
                   sizeof(error_filter)) < 0) {
    SetError(error, SystemError("enable CAN error frames failed"));
    ::close(socket_fd);
    return false;
  }
  const int enable_timestamp = 1;
  if (::setsockopt(socket_fd, SOL_SOCKET, SO_TIMESTAMPNS, &enable_timestamp,
                   sizeof(enable_timestamp)) < 0) {
    SetError(error, SystemError("enable SocketCAN kernel RX timestamps failed"));
    ::close(socket_fd);
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

bool SocketCan::Send(const SocketCanFrame& frame, std::string* error) const {
  if (!IsOpen()) {
    SetError(error, "CAN socket is closed");
    return false;
  }
  if (!frame.IsValidForSend()) {
    SetError(error, "invalid CAN frame");
    return false;
  }

  struct canfd_frame native_frame {};
  native_frame.can_id = frame.id;
  if (frame.extended) {
    native_frame.can_id |= CAN_EFF_FLAG;
  }
  if (frame.remote) {
    native_frame.can_id |= CAN_RTR_FLAG;
  }
  native_frame.len = frame.length;
  if (frame.brs) {
    native_frame.flags |= CANFD_BRS;
  }
  if (frame.esi) {
    native_frame.flags |= CANFD_ESI;
  }
  std::memcpy(native_frame.data, frame.data.data(), frame.length);

  const std::size_t native_size = frame.fd ? CANFD_MTU : CAN_MTU;

  ssize_t written = -1;
  do {
    written = ::write(fd_, &native_frame, native_size);
  } while (written < 0 && errno == EINTR);

  if (written != static_cast<ssize_t>(native_size)) {
    SetError(error, SystemError("send CAN frame failed"));
    return false;
  }
  return true;
}

CanIoStatus SocketCan::Receive(SocketCanFrame* frame, int timeout_ms, std::string* error) const {
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

  struct canfd_frame native_frame {};
  struct iovec io_vector {};
  io_vector.iov_base = &native_frame;
  io_vector.iov_len = sizeof(native_frame);
  alignas(struct cmsghdr) std::array<std::uint8_t, CMSG_SPACE(sizeof(struct timespec))> control{};
  struct msghdr message {};
  message.msg_iov = &io_vector;
  message.msg_iovlen = 1;
  message.msg_control = control.data();
  message.msg_controllen = control.size();
  ssize_t received = -1;
  do {
    message.msg_controllen = control.size();
    message.msg_flags = 0;
    received = ::recvmsg(fd_, &message, 0);
  } while (received < 0 && errno == EINTR);

  if (received != CAN_MTU && received != CANFD_MTU) {
    SetError(error, SystemError("receive CAN frame failed"));
    return CanIoStatus::kError;
  }
  if ((message.msg_flags & MSG_CTRUNC) != 0) {
    SetError(error, "SocketCAN kernel RX timestamp was truncated");
    return CanIoStatus::kError;
  }

  struct timespec kernel_timestamp {};
  bool kernel_timestamp_found = false;
  for (struct cmsghdr* control_message = CMSG_FIRSTHDR(&message); control_message != nullptr;
       control_message = CMSG_NXTHDR(&message, control_message)) {
    if (control_message->cmsg_level == SOL_SOCKET &&
        control_message->cmsg_type == SCM_TIMESTAMPNS &&
        control_message->cmsg_len >= CMSG_LEN(sizeof(struct timespec))) {
      std::memcpy(&kernel_timestamp, CMSG_DATA(control_message), sizeof(kernel_timestamp));
      kernel_timestamp_found = true;
      break;
    }
  }
  struct timespec sampled_realtime {};
  struct timespec sampled_steady {};
  std::int64_t kernel_realtime_ns = 0;
  std::int64_t sampled_realtime_ns = 0;
  std::int64_t sampled_steady_ns = 0;
  std::int64_t received_steady_ns = 0;
  if (!kernel_timestamp_found || ::clock_gettime(CLOCK_REALTIME, &sampled_realtime) != 0 ||
      ::clock_gettime(CLOCK_MONOTONIC, &sampled_steady) != 0 ||
      !TimespecNanoseconds(kernel_timestamp, &kernel_realtime_ns) ||
      !TimespecNanoseconds(sampled_realtime, &sampled_realtime_ns) ||
      !TimespecNanoseconds(sampled_steady, &sampled_steady_ns) ||
      !MapKernelRealtimeToSteady(kernel_realtime_ns, sampled_realtime_ns, sampled_steady_ns,
                                 &received_steady_ns)) {
    SetError(error, "SocketCAN kernel RX timestamp is missing or cannot map to steady time");
    return CanIoStatus::kError;
  }

  *frame = SocketCanFrame{};
  frame->kernel_timestamp_valid = true;
  frame->kernel_realtime_ns = kernel_realtime_ns;
  frame->received_steady_ns = received_steady_ns;
  frame->dequeued_steady_ns = sampled_steady_ns;
  const bool extended = (native_frame.can_id & CAN_EFF_FLAG) != 0;
  frame->extended = extended;
  frame->remote = (native_frame.can_id & CAN_RTR_FLAG) != 0;
  frame->error = (native_frame.can_id & CAN_ERR_FLAG) != 0;
  frame->id = native_frame.can_id & (extended ? CAN_EFF_MASK : CAN_SFF_MASK);
  frame->error_mask = frame->error ? native_frame.can_id & CAN_ERR_MASK : 0U;
  frame->bus_off = frame->error && (frame->error_mask & CAN_ERR_BUSOFF) != 0U;
  frame->error_passive =
      frame->error && (frame->error_mask & CAN_ERR_CRTL) != 0U &&
      (native_frame.data[1] & (CAN_ERR_CRTL_RX_PASSIVE | CAN_ERR_CRTL_TX_PASSIVE)) != 0U;
  frame->error_warning =
      frame->error && (frame->error_mask & CAN_ERR_CRTL) != 0U &&
      (native_frame.data[1] & (CAN_ERR_CRTL_RX_WARNING | CAN_ERR_CRTL_TX_WARNING)) != 0U;
  frame->ack_error = frame->error && (frame->error_mask & CAN_ERR_ACK) != 0U;
  frame->protocol_error = frame->error && (frame->error_mask & CAN_ERR_PROT) != 0U;
  frame->fd = received == CANFD_MTU;
  frame->brs = frame->fd && (native_frame.flags & CANFD_BRS) != 0U;
  frame->esi = frame->fd && (native_frame.flags & CANFD_ESI) != 0U;
  const std::uint8_t max_length = frame->fd ? CANFD_MAX_DLEN : CAN_MAX_DLEN;
  frame->length = native_frame.len > max_length ? max_length : native_frame.len;
  std::copy_n(native_frame.data, frame->length, frame->data.begin());
  return CanIoStatus::kOk;
}

void SocketCan::SetError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

}  // namespace can
}  // namespace cockpit
