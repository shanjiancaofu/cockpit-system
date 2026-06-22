#include "core/config/system_config.h"

#include <iostream>
#include <stdexcept>
#include <string>

int main() {
  const auto config = cockpit::config::SystemConfig::LoadFromFile(VALID_CONFIG_PATH);
  if (config.system().name != "cockpit-system" ||
      config.system().vehicle_id != "car_001" ||
      config.services().vehicle_data.grpc.listen_address != "127.0.0.1:50050" ||
      config.services().gateway.stream_timeout_ms != 10000 ||
      config.services().audio.grpc.listen_address != "127.0.0.1:50052" ||
      config.hardware().can.interface != "vcan0" || config.tools().topic.backend != "file") {
    std::cerr << "typed config fields do not match config.yaml" << std::endl;
    return 1;
  }

  try {
    cockpit::config::SystemConfig::LoadFromFile(INVALID_CONFIG_PATH);
    std::cerr << "invalid config was accepted" << std::endl;
    return 1;
  } catch (const std::runtime_error& error) {
    const std::string message = error.what();
    if (message.find("services.vehicle_data.grpc.listen_address") == std::string::npos) {
      std::cerr << "invalid config error did not identify YAML path: " << message << std::endl;
      return 1;
    }
  }

  try {
    cockpit::config::SystemConfig::LoadFromFile(INVALID_AUDIO_CONFIG_PATH);
    std::cerr << "invalid audio config was accepted" << std::endl;
    return 1;
  } catch (const std::runtime_error& error) {
    const std::string message = error.what();
    if (message.find("hardware.audio.capture_backend") == std::string::npos) {
      std::cerr << "invalid audio config error did not identify YAML path: " << message
                << std::endl;
      return 1;
    }
  }
  return 0;
}
