#pragma once

#include <cstdint>

namespace cockpit::vehicle {

enum class ChassisOdometryTimeMapStatus {
  kMapped,
  kReset,
  kInvalid,
};

// Extends the STM32 uint32 millisecond clock and anchors it to host realtime.
// The result is a host estimate, not a PTP-synchronized hardware timestamp.
class ChassisOdometryTimeMapper final {
 public:
  ChassisOdometryTimeMapStatus Map(std::uint32_t device_timestamp_ms,
                                   std::int64_t received_realtime_ns,
                                   std::int64_t* sample_realtime_ns);
  void Reset();

 private:
  bool initialized_ = false;
  std::uint32_t last_device_timestamp_ms_ = 0;
  std::uint64_t wrap_count_ = 0;
  std::int64_t realtime_offset_ns_ = 0;
  std::int64_t last_received_realtime_ns_ = 0;
  std::int64_t last_sample_realtime_ns_ = 0;
  bool explicit_reset_pending_ = false;
};

}  // namespace cockpit::vehicle
