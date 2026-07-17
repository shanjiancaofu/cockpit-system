#include "cockpit/library/carupload/carupload_runtime.h"

#include <exception>
#include <iostream>
#include <string>

#include "cockpit/core/config/system_config.h"
#include "cockpit/core/logging/logger.h"
#include "cockpit/modules/vehicle/vehicle_state.h"

namespace cockpit {
namespace carupload {

bool CaruploadRuntime::Start(const std::string& config_path) {
  if (running_.load()) {
    return false;
  }
  try {
    const auto config = config::SystemConfig::LoadFromFile(config_path);
    const auto& mqtt = config.services().cloud_uplink.mqtt;
    logging::InitLogger("carupload", config.paths().log_dir,
                        logging::ParseLevel(config.logging().level), config.logging().mirror_stderr,
                        config.logging().dump_time_secs, config.logging().cut_off_time_mins,
                        config.logging().max_files);
    const auto state = vehicle::MakeMockVehicleState(1);
    LOG_INFO("carupload plan vehicle_id=" + config.system().vehicle_id + " broker=" + mqtt.broker);
    LOG_WARN("MQTT transport is a placeholder; next step is Paho or Mosquitto integration");
    std::cout << "would publish topic=" << mqtt.telemetry_topic << " payload=" << state.ToJson()
              << std::endl;
  } catch (const std::exception& error) {
    LOG_ERROR("failed to configure carupload: " + std::string(error.what()));
    return false;
  }
  running_.store(true);
  return true;
}

void CaruploadRuntime::Stop() {
  running_.store(false);
}

int CaruploadRuntime::Poll() const {
  return running_.load() ? 0 : 1;
}

}  // namespace carupload
}  // namespace cockpit
