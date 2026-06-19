#include "common/utils/Time.h"

#include <chrono>

namespace cockpit {
namespace utils {

std::int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace utils
}  // namespace cockpit
