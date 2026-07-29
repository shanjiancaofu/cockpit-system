#include "cockpit/navigator/common/module_id.h"

#include <array>
#include <utility>

namespace cockpit {
namespace navigator {
namespace {

constexpr std::array<std::pair<ModuleId, const char*>, 12> kModuleNames{{
    {ModuleId::kTransfer, "transfer"},
    {ModuleId::kVehicleDriver, "vehicle_driver"},
    {ModuleId::kAudioDriver, "audio_driver"},
    {ModuleId::kCameraDriver, "camera_driver"},
    {ModuleId::kAgent, "agent"},
    {ModuleId::kHmi, "hmi"},
    {ModuleId::kCarupload, "carupload"},
    {ModuleId::kRecording, "recording"},
    {ModuleId::kUpgrader, "upgrader"},
    {ModuleId::kDebugger, "debugger"},
    {ModuleId::kCalibration, "calibration"},
    {ModuleId::kWatchdog, "watchdog"},
}};

}  // namespace

const char* ModuleName(ModuleId module) {
  for (const auto& entry : kModuleNames) {
    if (entry.first == module) {
      return entry.second;
    }
  }
  return "unknown";
}

bool ParseModuleId(const std::string& name, ModuleId* module) {
  for (const auto& entry : kModuleNames) {
    if (name == entry.second) {
      if (module != nullptr) {
        *module = entry.first;
      }
      return true;
    }
  }
  return false;
}

}  // namespace navigator
}  // namespace cockpit
