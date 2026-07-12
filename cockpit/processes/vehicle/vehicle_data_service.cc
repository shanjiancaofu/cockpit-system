#include "vehicle_data_service.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

#include "cockpit/core/logging/logger.h"
#include "cockpit/drivers/socketcan/socket_can.h"
#include "cockpit/modules/vehicle/vehicle_can_codec.h"

namespace cockpit {
namespace vehicle {

VehicleDataService::VehicleDataService(runtime::ProcessRuntime& runtime, StateSink state_sink)
    : runtime_(runtime), state_sink_(std::move(state_sink)) {
}

int VehicleDataService::Run() {
  const auto& vehicle_config = runtime_.config().services().vehicle_data;
  const std::string source = runtime_.args().GetString("source", vehicle_config.source);
  if (source == "mock") {
    return RunMock();
  }
  if (source == "socketcan") {
    return RunSocketCan();
  }
  LOG_ERROR("unsupported vehicle source: " + source);
  return 2;
}

int VehicleDataService::RunMock() {
  const int interval_ms = runtime_.config().services().vehicle_data.publish_interval_ms;
  const int samples = runtime_.args().GetInt("samples", 5);
  const bool forever = runtime_.args().HasFlag("forever");

  int sequence = 0;
  do {
    Publish(MakeMockVehicleState(sequence));
    ++sequence;
    std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
  } while (!runtime_.ShouldStop() && (forever || sequence < samples));
  return 0;
}

int VehicleDataService::RunSocketCan() {
  const auto& config = runtime_.config();
  const auto& can_config = config.hardware().can;
  const std::string& interface_name = can_config.interface;
  const int timeout_ms = can_config.receive_timeout_ms;
  const int max_idle_timeouts = can_config.max_idle_timeouts;
  const int samples = runtime_.args().GetInt("samples", 5);
  const bool forever = runtime_.args().HasFlag("forever");

  can::SocketCan socket;
  std::string error;
  if (!socket.Open(interface_name, &error)) {
    LOG_ERROR(error);
    return 1;
  }
  LOG_INFO("vehicle source opened interface=" + interface_name);

  int published = 0;
  int idle_timeouts = 0;
  while (!runtime_.ShouldStop() && (forever || published < samples)) {
    can::CanFrame frame;
    error.clear();
    const can::CanIoStatus io_status = socket.Receive(&frame, timeout_ms, &error);
    if (io_status == can::CanIoStatus::kTimeout) {
      if (!forever && ++idle_timeouts >= max_idle_timeouts) {
        LOG_ERROR("CAN receive timed out before enough vehicle frames arrived");
        return 3;
      }
      continue;
    }
    if (io_status != can::CanIoStatus::kOk) {
      LOG_ERROR(error.empty() ? "CAN receive failed" : error);
      return 1;
    }

    VehicleState state;
    const VehicleCanDecodeStatus decode_status = VehicleCanCodec::Decode(frame, &state);
    if (decode_status == VehicleCanDecodeStatus::kIgnored) {
      LOG_DEBUG("ignore CAN frame " + frame.ToString());
      continue;
    }
    if (decode_status == VehicleCanDecodeStatus::kInvalid) {
      LOG_WARN("invalid vehicle CAN frame " + frame.ToString());
      continue;
    }

    idle_timeouts = 0;
    Publish(state);
    ++published;
  }
  return 0;
}

void VehicleDataService::Publish(const VehicleState& state) const {
  const std::string json = state.ToJson();
  LOG_INFO("publish VehicleState " + json);
  std::cout << json << std::endl;
  if (state_sink_) {
    state_sink_(state);
  }
}

}  // namespace vehicle
}  // namespace cockpit
