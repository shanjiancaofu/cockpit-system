#pragma once

#include <cstdint>
#include <string>

namespace cockpit {
namespace navigator {

enum class ModuleId : std::uint8_t {
  kUnknown = 0,
  kTransfer = 1,
  kVehicleDriver = 10,
  kAudioDriver = 11,
  kCameraDriver = 12,
  kAgent = 20,
  kHmi = 21,
  kCarupload = 22,
  kRecording = 23,
  kMedia = 24,
  kSentinel = 25,
  kUpgrader = 30,
  kDebugger = 31,
  kCalibration = 32,
  kWatchdog = 33,
};

const char* ModuleName(ModuleId module);
bool ParseModuleId(const std::string& name, ModuleId* module);

}  // namespace navigator
}  // namespace cockpit
