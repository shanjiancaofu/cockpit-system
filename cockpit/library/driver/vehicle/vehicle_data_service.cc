#include "vehicle_data_service.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

#include "cockpit/core/logging/logger.h"
#include "cockpit/core/time/time.h"
#include "cockpit/drivers/socketcan/socket_can.h"
#include "cockpit/modules/can/socket_can_adapter.h"
#include "cockpit/modules/vehicle/chassis_client.h"
#include "cockpit/modules/vehicle/vehicle_can_codec.h"

namespace cockpit {
namespace vehicle {

VehicleDataService::VehicleDataService(VehicleDataOptions options, StateSink state_sink,
                                       ContinueHandler should_continue,
                                       LinkStatusSink link_status_sink,
                                       ChassisStateSink chassis_state_sink)
    : options_(std::move(options)),
      state_sink_(std::move(state_sink)),
      should_continue_(std::move(should_continue)),
      link_status_sink_(std::move(link_status_sink)),
      chassis_state_sink_(std::move(chassis_state_sink)) {
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
  can::CanLinkStatus link_status;
  link_status.interface_name = "mock";
  link_status.state = can::CanCommunicationState::kOnline;
  if (link_status_sink_) {
    link_status_sink_(link_status);
  }
  int sequence = 0;
  do {
    Publish(MakeMockVehicleState(sequence));
    ++link_status.rx_frames;
    ++link_status.decoded_frames;
    link_status.last_rx_timestamp_ms = static_cast<std::uint64_t>(time::WallNowMs());
    if (link_status_sink_) {
      link_status_sink_(link_status);
    }
    ++sequence;
    std::this_thread::sleep_for(std::chrono::milliseconds(options_.vehicle.publish_interval_ms));
  } while (should_continue_() && (options_.forever || sequence < options_.samples));
  return 0;
}

int VehicleDataService::RunSocketCan() {
  const std::string& interface_name = options_.can.interface;
  const int timeout_ms = options_.can.receive_timeout_ms;
  const int poll_timeout_ms = std::max(1, std::min(timeout_ms, 50));
  const int max_idle_timeouts = options_.can.max_idle_timeouts;

  can::SocketCan socket;
  can::CanLinkStatus link_status;
  ChassisClient chassis_client;
  link_status.interface_name = interface_name;
  link_status.fd_enabled = true;
  const auto publish_link_status = [this, &link_status] {
    if (link_status_sink_) {
      link_status_sink_(link_status);
    }
  };
  std::string error;
  if (!socket.Open(interface_name, &error)) {
    LOG_ERROR(error);
    return 1;
  }
  publish_link_status();
  LOG_INFO("vehicle source opened interface=" + interface_name);

  int published = 0;
  int idle_timeouts = 0;
  int idle_elapsed_ms = 0;
  while (should_continue_() && (options_.forever || published < options_.samples)) {
    const std::int64_t now_ms = time::SteadyNowMs();
    ChassisState heartbeat_state;
    if (chassis_client.Update(now_ms, &heartbeat_state)) {
      PublishChassis(heartbeat_state);
    }
    if (chassis_client.HeartbeatDue(now_ms)) {
      can::CanFrame heartbeat;
      if (!chassis_client.BuildHeartbeat(now_ms, &heartbeat) ||
          !socket.Send(can::ToSocketCanFrame(heartbeat), &error)) {
        link_status.state = can::CanCommunicationState::kFaulted;
        link_status.last_error = error.empty() ? "send chassis heartbeat failed" : error;
        publish_link_status();
        LOG_ERROR(link_status.last_error);
        return 1;
      }
    }
    can::SocketCanFrame socket_frame;
    error.clear();
    const can::CanIoStatus io_status = socket.Receive(&socket_frame, poll_timeout_ms, &error);
    if (io_status == can::CanIoStatus::kTimeout) {
      idle_elapsed_ms += poll_timeout_ms;
      if (idle_elapsed_ms >= timeout_ms) {
        idle_elapsed_ms = 0;
        ++idle_timeouts;
        ++link_status.idle_timeouts;
        if (idle_timeouts >= max_idle_timeouts) {
          link_status.state = can::CanCommunicationState::kIdle;
          link_status.last_error = "CAN receive idle threshold exceeded";
          publish_link_status();
        }
        if (!options_.forever && idle_timeouts >= max_idle_timeouts) {
          LOG_ERROR("CAN receive timed out before enough vehicle frames arrived");
          return 3;
        }
      }
      continue;
    }
    if (io_status != can::CanIoStatus::kOk) {
      link_status.state = can::CanCommunicationState::kFaulted;
      link_status.last_error = error.empty() ? "CAN receive failed" : error;
      publish_link_status();
      LOG_ERROR(error.empty() ? "CAN receive failed" : error);
      return 1;
    }

    ++link_status.rx_frames;
    idle_elapsed_ms = 0;
    link_status.last_rx_timestamp_ms = static_cast<std::uint64_t>(time::WallNowMs());

    if (socket_frame.error) {
      ++link_status.error_frames;
      link_status.bus_off_count += socket_frame.bus_off ? 1U : 0U;
      link_status.error_passive_count += socket_frame.error_passive ? 1U : 0U;
      link_status.error_warning_count += socket_frame.error_warning ? 1U : 0U;
      link_status.ack_error_count += socket_frame.ack_error ? 1U : 0U;
      link_status.protocol_error_count += socket_frame.protocol_error ? 1U : 0U;
      link_status.state = socket_frame.bus_off ? can::CanCommunicationState::kFaulted
                                               : can::CanCommunicationState::kIdle;
      link_status.last_error =
          "SocketCAN error frame mask=" + std::to_string(socket_frame.error_mask);
      publish_link_status();
      LOG_ERROR(link_status.last_error);
      continue;
    }
    can::CanFrame frame;
    if (!can::FromSocketCanFrame(socket_frame, &frame, &error)) {
      ++link_status.invalid_frames;
      link_status.last_error = error.empty() ? "invalid SocketCAN frame" : error;
      publish_link_status();
      LOG_WARN(error.empty() ? "invalid SocketCAN frame" : error);
      continue;
    }

    ChassisState chassis_state;
    const ChassisClientDecodeStatus chassis_status =
        chassis_client.ProcessFrame(frame, time::SteadyNowMs(), &chassis_state);
    if (chassis_status == ChassisClientDecodeStatus::kUpdated) {
      idle_timeouts = 0;
      link_status.state = can::CanCommunicationState::kOnline;
      link_status.last_error.clear();
      ++link_status.decoded_frames;
      publish_link_status();
      PublishChassis(chassis_state);
      ++published;
      continue;
    }
    if (chassis_status == ChassisClientDecodeStatus::kInvalid) {
      ++link_status.invalid_frames;
      link_status.last_error = "invalid chassis CAN FD frame";
      publish_link_status();
      LOG_WARN("invalid chassis CAN FD frame " + frame.ToString());
      continue;
    }

    VehicleState state;
    const VehicleCanDecodeStatus decode_status = VehicleCanCodec::Decode(frame, &state);
    if (decode_status == VehicleCanDecodeStatus::kIgnored) {
      LOG_DEBUG("ignore CAN frame " + frame.ToString());
      continue;
    }
    if (decode_status == VehicleCanDecodeStatus::kInvalid) {
      ++link_status.invalid_frames;
      link_status.last_error = "invalid vehicle CAN frame";
      publish_link_status();
      LOG_WARN("invalid vehicle CAN frame " + frame.ToString());
      continue;
    }

    idle_timeouts = 0;
    link_status.state = can::CanCommunicationState::kOnline;
    link_status.last_error.clear();
    ++link_status.decoded_frames;
    publish_link_status();
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

void VehicleDataService::PublishChassis(const ChassisState& state) const {
  const std::string json = state.ToJson();
  LOG_INFO("publish ChassisState " + json);
  std::cout << json << std::endl;
  if (chassis_state_sink_) {
    chassis_state_sink_(state);
  }
}

}  // namespace vehicle
}  // namespace cockpit
