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

VehicleDataService::VehicleDataService(VehicleDataOptions options, StateSink state_sink,
                                       ContinueHandler should_continue)
    : options_(std::move(options)),
      state_sink_(std::move(state_sink)),
      should_continue_(std::move(should_continue)) {
}

int VehicleDataService::Run() {
  if (options_.source == "mock") {
    return RunMock();
  }
  if (options_.source == "socketcan") {
    return RunSocketCan();
  }
  LOG_ERROR("unsupported vehicle source: " + options_.source);
  return 2;
}

int VehicleDataService::RunMock() {
  int sequence = 0;
  do {
    Publish(MakeMockVehicleState(sequence));
    ++sequence;
    std::this_thread::sleep_for(std::chrono::milliseconds(options_.vehicle.publish_interval_ms));
  } while (should_continue_() && (options_.forever || sequence < options_.samples));
  return 0;
}

int VehicleDataService::RunSocketCan() {
  const std::string& interface_name = options_.can.interface;
  const int timeout_ms = options_.can.receive_timeout_ms;
  const int max_idle_timeouts = options_.can.max_idle_timeouts;

  can::SocketCan socket;
  std::string error;
  if (!socket.Open(interface_name, &error)) {
    LOG_ERROR(error);
    return 1;
  }
  LOG_INFO("vehicle source opened interface=" + interface_name);

  int published = 0;
  int idle_timeouts = 0;
  while (should_continue_() && (options_.forever || published < options_.samples)) {
    can::CanFrame frame;
    error.clear();
    const can::CanIoStatus io_status = socket.Receive(&frame, timeout_ms, &error);
    if (io_status == can::CanIoStatus::kTimeout) {
      if (!options_.forever && ++idle_timeouts >= max_idle_timeouts) {
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
