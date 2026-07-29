#pragma once

#include <cstdint>
#include <string>

namespace cockpit {
namespace navigator {

enum class RunMode : std::uint8_t {
  kUnknown = 0,
  kNormal = 1,
  kDevelopment = 2,
  kUi = 3,
  kCloud = 4,
  kUpgrade = 5,
};

const char* RunModeName(RunMode mode);
bool ParseRunMode(const std::string& name, RunMode* mode);

}  // namespace navigator
}  // namespace cockpit
