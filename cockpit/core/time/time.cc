#include "cockpit/core/time/time.h"

#include <chrono>

namespace cockpit {
namespace time {

WallTime WallTime::Now() {
  return WallTime(WallNowNs());
}

std::int64_t WallTime::ToMilliseconds() const {
  return nanoseconds_ / 1000000;
}

std::int64_t WallTime::ToNanoseconds() const {
  return nanoseconds_;
}

SteadyTime SteadyTime::Now() {
  return SteadyTime(SteadyNowNs());
}

std::int64_t SteadyTime::ToMilliseconds() const {
  return nanoseconds_ / 1000000;
}

std::int64_t SteadyTime::ToNanoseconds() const {
  return nanoseconds_;
}

std::int64_t WallNowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::int64_t WallNowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::int64_t SteadyNowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::int64_t SteadyNowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::int64_t NowMs() {
  return WallNowMs();
}

std::int64_t NowNs() {
  return WallNowNs();
}

}  // namespace time
}  // namespace cockpit
