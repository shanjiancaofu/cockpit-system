#include "cockpit/core/time/time.h"

#include <chrono>

namespace cockpit {
namespace time {

WallTime WallTime::Now() {
  return WallTime(std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count());
}

std::int64_t WallTime::ToMilliseconds() const {
  return nanoseconds_ / 1000000;
}

std::int64_t WallTime::ToNanoseconds() const {
  return nanoseconds_;
}

SteadyTime SteadyTime::Now() {
  return SteadyTime(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count());
}

std::int64_t SteadyTime::ToMilliseconds() const {
  return nanoseconds_ / 1000000;
}

std::int64_t SteadyTime::ToNanoseconds() const {
  return nanoseconds_;
}

}  // namespace time
}  // namespace cockpit
