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

}  // namespace time
}  // namespace cockpit
