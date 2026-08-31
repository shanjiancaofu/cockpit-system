#include "cockpit/modules/camera/capture/gstreamer_timestamp_mapper.h"

namespace cockpit {
namespace camera {

bool MapGstreamerRunningTimeToRealtime(std::int64_t sample_running_time_ns,
                                       std::int64_t current_running_time_ns,
                                       std::int64_t realtime_before_ns,
                                       std::int64_t realtime_after_ns,
                                       std::int64_t* realtime_sample_ns) {
  if (realtime_sample_ns == nullptr || sample_running_time_ns < 0 ||
      current_running_time_ns < sample_running_time_ns || realtime_before_ns <= 0 ||
      realtime_after_ns < realtime_before_ns) {
    return false;
  }

  const std::int64_t realtime_midpoint_ns =
      realtime_before_ns + (realtime_after_ns - realtime_before_ns) / 2;
  const std::int64_t sample_age_ns = current_running_time_ns - sample_running_time_ns;
  if (sample_age_ns >= realtime_midpoint_ns) {
    return false;
  }
  *realtime_sample_ns = realtime_midpoint_ns - sample_age_ns;
  return true;
}

}  // namespace camera
}  // namespace cockpit
