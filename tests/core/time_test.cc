#include "cockpit/core/time/time.h"

#include <chrono>
#include <cstdint>
#include <iostream>

int main() {
  const std::int64_t system_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count();
  const std::int64_t wall_ms = cockpit::time::WallTime::Now().ToMilliseconds();
  const std::int64_t steady_before_ms = cockpit::time::SteadyTime::Now().ToMilliseconds();
  const std::int64_t steady_after_ms = cockpit::time::SteadyTime::Now().ToMilliseconds();
  if (wall_ms < system_ms - 1000 || wall_ms > system_ms + 1000) {
    std::cerr << "wall clock is not in the Unix epoch domain\n";
    return 1;
  }
  if (steady_after_ms < steady_before_ms) {
    std::cerr << "steady clock moved backwards\n";
    return 1;
  }
  if (cockpit::time::NowMs() < wall_ms) {
    std::cerr << "legacy NowMs alias is not a wall clock\n";
    return 1;
  }
  return 0;
}
