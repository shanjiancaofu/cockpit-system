#include "cockpit/navigator/run_config/run_config.h"

#include <filesystem>
#include <unordered_set>

namespace cockpit {
namespace navigator {
namespace {

bool IsValidLibraryName(const std::string& library) {
  const std::filesystem::path path(library);
  return !library.empty() && !path.is_absolute() && !path.has_parent_path() &&
         path.extension() == ".so";
}

}  // namespace

RunConfig RunConfig::Default() {
  RunConfig config;
  config.socket_path = "/tmp/cockpit-navigator.sock";
  config.modules = {
      {ModuleId::kTransfer, "libtransfer.so", true},
      {ModuleId::kVehicleDriver, "libvehicle_driver.so", true},
      {ModuleId::kAudioDriver, "libaudio_driver.so", true},
      {ModuleId::kCameraDriver, "libcamera_driver.so", true},
      {ModuleId::kAgent, "libagent.so", true},
      {ModuleId::kHmi, "libhmi.so"},
      {ModuleId::kCarupload, "libcarupload.so"},
      {ModuleId::kRecording, "librecording.so"},
      {ModuleId::kMedia, "libmedia.so"},
      {ModuleId::kSentinel, "libsentinel.so"},
      {ModuleId::kBridge, "libbridge.so"},
      {ModuleId::kUpgrader, "libupgrader.so", true},
      {ModuleId::kDebugger, "libdebugger.so"},
      {ModuleId::kCalibration, "libcalibration.so"},
      {ModuleId::kWatchdog, "libwatchdog.so"},
  };
  config.modes = {
      {RunMode::kNormal,
       {ModuleId::kTransfer, ModuleId::kVehicleDriver, ModuleId::kAudioDriver,
        ModuleId::kCameraDriver, ModuleId::kAgent, ModuleId::kRecording, ModuleId::kSentinel}},
      {RunMode::kDevelopment,
       {ModuleId::kTransfer, ModuleId::kVehicleDriver, ModuleId::kAudioDriver,
        ModuleId::kCameraDriver, ModuleId::kAgent, ModuleId::kRecording, ModuleId::kSentinel,
        ModuleId::kBridge}},
      {RunMode::kUi,
       {ModuleId::kTransfer, ModuleId::kVehicleDriver, ModuleId::kAudioDriver,
        ModuleId::kCameraDriver, ModuleId::kAgent, ModuleId::kMedia, ModuleId::kRecording,
        ModuleId::kSentinel, ModuleId::kHmi}},
      {RunMode::kCloud, {ModuleId::kTransfer, ModuleId::kVehicleDriver, ModuleId::kCarupload}},
      {RunMode::kUpgrade, {ModuleId::kUpgrader}},
  };
  return config;
}

bool RunConfig::Validate(std::string* error) const {
  if (error == nullptr) {
    return false;
  }
  error->clear();
  if (socket_path.empty() || !std::filesystem::path(socket_path).is_absolute()) {
    *error = "Navigator socket path must be absolute";
    return false;
  }
  if (startup_timeout_ms <= 0 || stop_timeout_ms <= 0 || restart_window_seconds <= 0) {
    *error = "Navigator timeouts and restart window must be positive";
    return false;
  }
  if (modules.empty()) {
    *error = "Navigator module list is empty";
    return false;
  }

  std::unordered_set<ModuleId> configured_modules;
  for (const ModuleConfig& module : modules) {
    const std::string module_name = ModuleName(module.id);
    if (module.id == ModuleId::kUnknown || module_name == "unknown") {
      *error = "Navigator module has an unknown id";
      return false;
    }
    if (!configured_modules.insert(module.id).second) {
      *error = "duplicate Navigator module: " + module_name;
      return false;
    }
    if (!IsValidLibraryName(module.library)) {
      *error = "invalid library name for module " + module_name + ": " + module.library;
      return false;
    }
    if (module.restart_limit < 0 || module.restart_delay_ms < 0) {
      *error = "invalid restart policy for module " + module_name;
      return false;
    }
  }

  if (modes.empty()) {
    *error = "Navigator mode list is empty";
    return false;
  }
  for (const auto& entry : modes) {
    const RunMode mode = entry.first;
    if (mode == RunMode::kUnknown || std::string(RunModeName(mode)) == "unknown") {
      *error = "Navigator has an unknown mode";
      return false;
    }
    if (entry.second.empty()) {
      *error = "Navigator mode is empty: " + std::string(RunModeName(mode));
      return false;
    }
    std::unordered_set<ModuleId> mode_modules;
    for (ModuleId module : entry.second) {
      if (configured_modules.find(module) == configured_modules.end()) {
        *error = "mode " + std::string(RunModeName(mode)) + " references unconfigured module " +
                 ModuleName(module);
        return false;
      }
      if (!mode_modules.insert(module).second) {
        *error = "mode " + std::string(RunModeName(mode)) + " contains duplicate module " +
                 ModuleName(module);
        return false;
      }
    }
  }
  if (FindMode(initial_mode) == nullptr) {
    *error = "initial Navigator mode is not configured: " + std::string(RunModeName(initial_mode));
    return false;
  }
  return true;
}

const ModuleConfig* RunConfig::FindModule(ModuleId id) const {
  for (const ModuleConfig& module : modules) {
    if (module.id == id) {
      return &module;
    }
  }
  return nullptr;
}

const std::vector<ModuleId>* RunConfig::FindMode(RunMode mode) const {
  const auto iter = modes.find(mode);
  return iter == modes.end() ? nullptr : &iter->second;
}

}  // namespace navigator
}  // namespace cockpit
