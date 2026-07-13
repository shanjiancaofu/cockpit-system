#include "cockpit/navigator/run_config/run_config.h"

#include <string>

namespace cockpit {
namespace navigator {

RunConfig RunConfig::Default() {
  RunConfig config;
  config.initial_mode = "normal";
  config.socket_path = "/tmp/cockpit-navigator.sock";
  config.modules = {
      {"transfer", "libtransfer.so"},
      {"vehicle_driver", "libvehicle_driver.so"},
      {"audio_driver", "libaudio_driver.so"},
      {"camera_driver", "libcamera_driver.so"},
      {"agent", "libagent.so"},
      {"hmi", "libhmi.so"},
      {"carupload", "libcarupload.so"},
      {"recording", "librecording.so"},
      {"upgrader", "libupgrader.so"},
      {"debugger", "libdebugger.so"},
      {"calibration", "libcalibration.so"},
      {"watchdog", "libwatchdog.so"},
  };
  config.modes = {
      {"normal", {"transfer", "vehicle_driver", "audio_driver", "camera_driver", "agent"}},
      {"development",
       {"transfer", "vehicle_driver", "audio_driver", "camera_driver", "agent", "recording"}},
      {"cloud", {"transfer", "vehicle_driver", "carupload"}},
  };
  return config;
}

const ModuleConfig* RunConfig::FindModule(const std::string& name) const {
  for (const ModuleConfig& module : modules) {
    if (module.name == name) {
      return &module;
    }
  }
  return nullptr;
}

}  // namespace navigator
}  // namespace cockpit
