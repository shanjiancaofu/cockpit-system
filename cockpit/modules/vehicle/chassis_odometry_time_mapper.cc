#include "cockpit/modules/vehicle/chassis_odometry_time_mapper.h"

#include <limits>

namespace cockpit::vehicle {
namespace {

constexpr std::uint64_t kClockModulus = std::uint64_t{1} << 32U;
constexpr std::uint32_t kWrapThreshold = std::uint32_t{1} << 31U;
constexpr std::uint64_t kNanosecondsPerMillisecond = 1000000U;

bool DeviceNanoseconds(std::uint64_t wrap_count, std::uint32_t timestamp_ms, std::int64_t* result) {
  if (wrap_count > std::numeric_limits<std::uint64_t>::max() / kClockModulus) return false;
  const std::uint64_t extended_ms = wrap_count * kClockModulus + timestamp_ms;
  if (extended_ms > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) /
                        kNanosecondsPerMillisecond) {
    return false;
  }
  *result = static_cast<std::int64_t>(extended_ms * kNanosecondsPerMillisecond);
  return true;
}

}  // namespace

ChassisOdometryTimeMapStatus ChassisOdometryTimeMapper::Map(std::uint32_t device_timestamp_ms,
                                                            std::int64_t received_realtime_ns,
                                                            std::int64_t* sample_realtime_ns) {
  if (sample_realtime_ns == nullptr || received_realtime_ns <= 0 ||
      (initialized_ && received_realtime_ns < last_received_realtime_ns_)) {
    return ChassisOdometryTimeMapStatus::kInvalid;
  }

  if (initialized_ && device_timestamp_ms < last_device_timestamp_ms_) {
    const std::uint32_t backwards = last_device_timestamp_ms_ - device_timestamp_ms;
    if (backwards > kWrapThreshold) {
      ++wrap_count_;
    } else {
      return ChassisOdometryTimeMapStatus::kInvalid;
    }
  }

  std::int64_t device_timestamp_ns = 0;
  if (!DeviceNanoseconds(wrap_count_, device_timestamp_ms, &device_timestamp_ns)) {
    return ChassisOdometryTimeMapStatus::kInvalid;
  }

  if (!initialized_) {
    realtime_offset_ns_ = received_realtime_ns - device_timestamp_ns;
  }
  if ((realtime_offset_ns_ > 0 &&
       device_timestamp_ns > std::numeric_limits<std::int64_t>::max() - realtime_offset_ns_) ||
      (realtime_offset_ns_ < 0 &&
       device_timestamp_ns < std::numeric_limits<std::int64_t>::min() - realtime_offset_ns_)) {
    return ChassisOdometryTimeMapStatus::kInvalid;
  }
  std::int64_t mapped_realtime_ns = realtime_offset_ns_ + device_timestamp_ns;
  if (mapped_realtime_ns > received_realtime_ns) {
    realtime_offset_ns_ -= mapped_realtime_ns - received_realtime_ns;
    mapped_realtime_ns = received_realtime_ns;
  }
  if (initialized_ && mapped_realtime_ns < last_sample_realtime_ns_) {
    return ChassisOdometryTimeMapStatus::kInvalid;
  }

  initialized_ = true;
  last_device_timestamp_ms_ = device_timestamp_ms;
  last_received_realtime_ns_ = received_realtime_ns;
  last_sample_realtime_ns_ = mapped_realtime_ns;
  *sample_realtime_ns = mapped_realtime_ns;
  const auto status = explicit_reset_pending_ ? ChassisOdometryTimeMapStatus::kReset
                                              : ChassisOdometryTimeMapStatus::kMapped;
  explicit_reset_pending_ = false;
  return status;
}

void ChassisOdometryTimeMapper::Reset() {
  initialized_ = false;
  last_device_timestamp_ms_ = 0;
  wrap_count_ = 0;
  realtime_offset_ns_ = 0;
  last_received_realtime_ns_ = 0;
  last_sample_realtime_ns_ = 0;
  explicit_reset_pending_ = true;
}

}  // namespace cockpit::vehicle
