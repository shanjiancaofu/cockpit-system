#include "cockpit/navigator/common/run_mode.h"

#include <array>
#include <utility>

namespace cockpit {
namespace navigator {
namespace {

constexpr std::array<std::pair<RunMode, const char*>, 5> kRunModeNames{{
    {RunMode::kNormal, "normal"},
    {RunMode::kDevelopment, "development"},
    {RunMode::kUi, "ui"},
    {RunMode::kCloud, "cloud"},
    {RunMode::kUpgrade, "upgrade"},
}};

}  // namespace

const char* RunModeName(RunMode mode) {
  for (const auto& entry : kRunModeNames) {
    if (entry.first == mode) {
      return entry.second;
    }
  }
  return "unknown";
}

bool ParseRunMode(const std::string& name, RunMode* mode) {
  for (const auto& entry : kRunModeNames) {
    if (name == entry.second) {
      if (mode != nullptr) {
        *mode = entry.first;
      }
      return true;
    }
  }
  return false;
}

}  // namespace navigator
}  // namespace cockpit
