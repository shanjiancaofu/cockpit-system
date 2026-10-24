#pragma once

#include <cstdint>

namespace cockpit {
namespace camera {

// Maps a sample running time into CLOCK_REALTIME from a near-simultaneous
// pipeline-running-time/realtime snapshot. Callback arrival time is not used
// as the anchor, so pipeline latency does not become a fixed timestamp bias.
bool MapGstreamerRunningTimeToRealtime(std::int64_t sample_running_time_ns,
                                       std::int64_t current_running_time_ns,
                                       std::int64_t realtime_before_ns,
                                       std::int64_t realtime_after_ns,
                                       std::int64_t* realtime_sample_ns);

}  // namespace camera
}  // namespace cockpit
