#include <iostream>
#include <string>

#include "cockpit/core/logging/Logger.h"
#include "cockpit/core/runtime/ServiceRuntime.h"
#include "cockpit/modules/vehicle/VehicleState.h"

int main(int argc, char** argv) {
  auto runtime = cockpit::runtime::ServiceRuntime::Create(argc, argv, "cloud-uplink-service");
  const auto& config = runtime.config();
  const auto& mqtt = config.services().cloud_uplink.mqtt;
  const std::string& broker = mqtt.broker;
  const std::string& topic = mqtt.telemetry_topic;
  const std::string& vehicle_id = config.system().vehicle_id;

  auto state = cockpit::vehicle::MakeMockVehicleState(1);
  LOG_INFO("cloud uplink plan vehicle_id=" + vehicle_id + " broker=" + broker);
  LOG_WARN("MQTT transport is a placeholder; next step is Paho or Mosquitto integration");
  std::cout << "would publish topic=" << topic << " payload=" << state.ToJson() << std::endl;
  runtime.MarkStopped();
  return 0;
}
