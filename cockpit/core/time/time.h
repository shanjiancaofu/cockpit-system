#pragma once

#include <cstdint>

namespace cockpit {
namespace time {

// Unix epoch timestamps for logs, events, manifests, and external APIs.
std::int64_t WallNowMs();
std::int64_t WallNowNs();

// Monotonic process-local timestamps for elapsed time, deadlines, and watchdogs.
std::int64_t SteadyNowMs();
std::int64_t SteadyNowNs();

// Backward-compatible wall-clock aliases.
std::int64_t NowMs();
std::int64_t NowNs();

}  // namespace time
}  // namespace cockpit
