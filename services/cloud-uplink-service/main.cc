#include "common/logging/Logger.h"
#include "common/runtime/ServiceRuntime.h"
#include "common/vehicle/VehicleState.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
  auto runtime = cockpit::runtime::ServiceRuntime::Create(argc, argv, "cloud-uplink-service");
  const auto& config = runtime.config();
  const std::string broker = config.GetString("mqtt.broker", "tcp://127.0.0.1:1883");
  const std::string topic = config.GetString("mqtt.telemetry_topic", "vehicle/status");
  const std::string vehicle_id = config.GetString("vehicle.id", "car_001");

  auto state = cockpit::vehicle::MakeMockVehicleState(1);
  LOG_INFO("cloud uplink plan vehicle_id=" + vehicle_id + " broker=" + broker);
  LOG_WARN("MQTT transport is a placeholder; next step is Paho or Mosquitto integration");
  std::cout << "would publish topic=" << topic << " payload=" << state.ToJson() << std::endl;
  runtime.MarkStopped();
  return 0;
}
