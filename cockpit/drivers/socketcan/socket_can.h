#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace cockpit {
namespace can {

enum class CanIoStatus : std::uint8_t {
  kOk,
  kTimeout,
  kClosed,
  kError,
};

struct SocketCanFrame {
  static constexpr std::size_t kMaxDataLength = 64;

  std::uint32_t id = 0;
  std::array<std::uint8_t, kMaxDataLength> data{};
  std::uint8_t length = 0;
  bool extended = false;
  bool remote = false;
  bool fd = false;
  bool brs = false;
  bool esi = false;
  bool error = false;
  std::uint32_t error_mask = 0;
  bool bus_off = false;
  bool error_passive = false;
  bool error_warning = false;
  bool ack_error = false;
  bool protocol_error = false;
  // SO_TIMESTAMPNS supplies kernel_realtime_ns. The driver maps it at dequeue
  // into CLOCK_MONOTONIC so safety freshness does not use processing time.
  bool kernel_timestamp_valid = false;
  std::int64_t kernel_realtime_ns = 0;
  std::int64_t received_steady_ns = 0;
  std::int64_t dequeued_steady_ns = 0;

  bool IsValidForSend() const;
  bool MapToLogicalSteadyMilliseconds(std::int64_t logical_dequeue_ms,
                                      std::int64_t* received_steady_ms) const;
};

bool MapKernelRealtimeToSteady(std::int64_t kernel_realtime_ns, std::int64_t sampled_realtime_ns,
                               std::int64_t sampled_steady_ns, std::int64_t* received_steady_ns);

class SocketCan {
 public:
  SocketCan() = default;
  ~SocketCan();

  SocketCan(const SocketCan&) = delete;
  SocketCan& operator=(const SocketCan&) = delete;

  SocketCan(SocketCan&& other) noexcept;
  SocketCan& operator=(SocketCan&& other) noexcept;

  bool Open(const std::string& interface_name, std::string* error);
  void Close();
  bool IsOpen() const {
    return fd_ >= 0;
  }

  bool Send(const SocketCanFrame& frame, std::string* error) const;
  CanIoStatus Receive(SocketCanFrame* frame, int timeout_ms, std::string* error) const;

 private:
  static void SetError(std::string* error, const std::string& message);

  int fd_ = -1;
};

}  // namespace can
}  // namespace cockpit
