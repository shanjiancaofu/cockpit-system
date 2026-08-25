#include "vehicle_grpc_service.h"

#include <algorithm>
#include <chrono>

#include "cockpit/core/logging/logger.h"
#include "cockpit/core/time/time.h"

namespace cockpit {
namespace vehicle {
namespace {

proto::vehicle::VehicleState ToProto(const VehicleState& state) {
  proto::vehicle::VehicleState message;
  message.set_timestamp_ms(state.timestamp_ms);
  message.set_speed_kph(state.speed_kph);
  message.set_gear(state.gear);
  message.set_soc_percent(state.soc_percent);
  message.set_cloud_enabled(state.cloud_enabled);
  message.set_source(state.source);
  return message;
}

proto::vehicle::ChassisEvent ToProto(const ChassisEvent& event) {
  proto::vehicle::ChassisEvent message;
  message.set_sequence(event.sequence);
  message.set_timestamp_ms(event.timestamp_ms);
  message.set_source(event.source);
  if (event.type == ChassisEventType::kMotionDetected) {
    message.set_type(proto::vehicle::CHASSIS_EVENT_TYPE_MOTION_DETECTED);
    message.mutable_motion()->set_sensor_id(event.sensor_id);
    message.mutable_motion()->set_detected(event.motion_detected);
  }
  return message;
}

proto::vehicle::CanCommunicationState ToProto(can::CanCommunicationState state) {
  switch (state) {
    case can::CanCommunicationState::kStarting:
      return proto::vehicle::CAN_COMMUNICATION_STATE_STARTING;
    case can::CanCommunicationState::kOnline:
      return proto::vehicle::CAN_COMMUNICATION_STATE_ONLINE;
    case can::CanCommunicationState::kIdle:
      return proto::vehicle::CAN_COMMUNICATION_STATE_IDLE;
    case can::CanCommunicationState::kFaulted:
      return proto::vehicle::CAN_COMMUNICATION_STATE_FAULTED;
  }
  return proto::vehicle::CAN_COMMUNICATION_STATE_UNSPECIFIED;
}

}  // namespace

VehicleGrpcService::~VehicleGrpcService() {
  Shutdown();
}

bool VehicleGrpcService::Start(const std::string& address) {
  grpc::ServerBuilder builder;
  builder.AddListeningPort(address, grpc::InsecureServerCredentials());
  builder.RegisterService(this);
  server_ = builder.BuildAndStart();
  if (!server_) {
    LOG_ERROR("failed to start vehicle gRPC server address=" + address);
    return false;
  }
  LOG_INFO("vehicle gRPC server listening address=" + address);
  return true;
}

void VehicleGrpcService::Publish(const VehicleState& state) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    latest_state_ = state;
    ++version_;
  }
  state_changed_.notify_all();
}

void VehicleGrpcService::PublishEvent(const ChassisEvent& event) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (events_.size() == kEventCapacity) {
      events_.pop_front();
      ++dropped_events_;
    }
    events_.push_back(VersionedEvent{++event_version_, ToProto(event)});
  }
  state_changed_.notify_all();
}

bool VehicleGrpcService::WaitForEventSubscriber(std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock(mutex_);
  return state_changed_.wait_for(lock, timeout, [this] {
    return stopping_ || event_subscribers_ > 0;
  }) && event_subscribers_ > 0;
}

void VehicleGrpcService::PublishLinkStatus(const can::CanLinkStatus& status) {
  std::lock_guard<std::mutex> lock(mutex_);
  link_status_ = status;
}

void VehicleGrpcService::Shutdown() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) {
      return;
    }
    stopping_ = true;
  }
  state_changed_.notify_all();
  if (server_) {
    server_->Shutdown();
    server_.reset();
  }
}

grpc::Status VehicleGrpcService::SubscribeVehicleState(
    grpc::ServerContext* context, const proto::vehicle::SubscribeVehicleStateRequest* request,
    grpc::ServerWriter<proto::vehicle::VehicleState>* writer) {
  const int requested_hz = request->max_hz() <= 0 ? 10 : request->max_hz();
  const int max_hz = std::clamp(requested_hz, 1, 100);
  const auto min_interval = std::chrono::milliseconds(1000 / max_hz);
  auto next_write = std::chrono::steady_clock::now();
  std::uint64_t observed_version = 0;

  LOG_INFO("vehicle state subscriber connected consumer=" + request->consumer());
  while (!context->IsCancelled()) {
    VehicleState state;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      state_changed_.wait_for(lock, std::chrono::milliseconds(100), [this, observed_version] {
        return stopping_ || version_ > observed_version;
      });
      if (stopping_) {
        break;
      }
      if (version_ <= observed_version) {
        continue;
      }
      if (std::chrono::steady_clock::now() < next_write) {
        state_changed_.wait_until(lock, next_write, [this] {
          return stopping_;
        });
        if (stopping_) {
          break;
        }
      }
      state = latest_state_;
      observed_version = version_;
    }

    if (!writer->Write(ToProto(state))) {
      break;
    }
    next_write = std::chrono::steady_clock::now() + min_interval;
  }
  LOG_INFO("vehicle state subscriber disconnected consumer=" + request->consumer());
  return grpc::Status::OK;
}

grpc::Status VehicleGrpcService::SubscribeChassisEvents(
    grpc::ServerContext* context, const proto::vehicle::SubscribeChassisEventsRequest* request,
    grpc::ServerWriter<proto::vehicle::ChassisEvent>* writer) {
  std::uint64_t observed_version = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    observed_version = event_version_;
    ++event_subscribers_;
  }
  state_changed_.notify_all();
  LOG_INFO("chassis event subscriber connected consumer=" + request->consumer());
  while (!context->IsCancelled()) {
    proto::vehicle::ChassisEvent event;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      state_changed_.wait_for(lock, std::chrono::milliseconds(100), [this, observed_version] {
        return stopping_ || event_version_ > observed_version;
      });
      if (stopping_) break;
      if (events_.empty() || event_version_ <= observed_version) continue;
      if (observed_version + 1 < events_.front().version) {
        observed_version = events_.front().version - 1;
      }
      const auto found =
          std::find_if(events_.begin(), events_.end(), [observed_version](const auto& entry) {
            return entry.version > observed_version;
          });
      if (found == events_.end()) continue;
      event = found->event;
      observed_version = found->version;
    }
    if (!writer->Write(event)) break;
  }
  LOG_INFO("chassis event subscriber disconnected consumer=" + request->consumer());
  {
    std::lock_guard<std::mutex> lock(mutex_);
    --event_subscribers_;
  }
  return grpc::Status::OK;
}

grpc::Status VehicleGrpcService::GetStatus(grpc::ServerContext*, const proto::common::Empty*,
                                           proto::vehicle::CanLinkStatus* response) {
  can::CanLinkStatus status;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    status = link_status_;
  }
  response->set_interface_name(status.interface_name);
  response->set_fd_enabled(status.fd_enabled);
  response->set_state(ToProto(status.state));
  const std::uint64_t now_ms = static_cast<std::uint64_t>(time::NowMs());
  response->set_last_rx_age_ms(status.last_rx_timestamp_ms == 0 ||
                                       now_ms < status.last_rx_timestamp_ms
                                   ? 0
                                   : now_ms - status.last_rx_timestamp_ms);
  response->set_rx_frames(status.rx_frames);
  response->set_decoded_frames(status.decoded_frames);
  response->set_invalid_frames(status.invalid_frames);
  response->set_idle_timeouts(status.idle_timeouts);
  response->set_error_frames(status.error_frames);
  response->set_bus_off_count(status.bus_off_count);
  response->set_error_passive_count(status.error_passive_count);
  response->set_error_warning_count(status.error_warning_count);
  response->set_ack_error_count(status.ack_error_count);
  response->set_protocol_error_count(status.protocol_error_count);
  response->set_last_error(status.last_error);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    response->set_chassis_events(event_version_);
    response->set_dropped_chassis_events(dropped_events_);
  }
  auto* health = response->mutable_health();
  health->set_service_name("vehicle-can-link");
  health->set_checked_at_ms(static_cast<std::int64_t>(now_ms));
  if (status.state == can::CanCommunicationState::kOnline) {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_OK);
    health->set_message("CAN link online");
  } else if (status.state == can::CanCommunicationState::kStarting) {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_DEGRADED);
    health->set_message("CAN link waiting for frames");
  } else if (status.state == can::CanCommunicationState::kIdle) {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_DEGRADED);
    health->set_message(status.last_error.empty() ? "CAN link idle" : status.last_error);
  } else {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_FAULTED);
    health->set_message(status.last_error.empty() ? "CAN link faulted" : status.last_error);
  }
  return grpc::Status::OK;
}

}  // namespace vehicle
}  // namespace cockpit
