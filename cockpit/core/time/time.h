#pragma once

#include <cstdint>

namespace cockpit {
namespace time {

class WallTime final {
 public:
  static WallTime Now();

  std::int64_t ToMilliseconds() const;
  std::int64_t ToNanoseconds() const;

 private:
  explicit WallTime(std::int64_t nanoseconds) : nanoseconds_(nanoseconds) {
  }

  std::int64_t nanoseconds_;
};

class SteadyTime final {
 public:
  static SteadyTime Now();

  std::int64_t ToMilliseconds() const;
  std::int64_t ToNanoseconds() const;

 private:
  explicit SteadyTime(std::int64_t nanoseconds) : nanoseconds_(nanoseconds) {
  }

  std::int64_t nanoseconds_;
};

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
