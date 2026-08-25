#include "cockpit/library/sentinel/chassis_event_client.h"

#include <chrono>
#include <thread>

namespace cockpit {
namespace sentinel {
namespace {

std::shared_ptr<grpc::Channel> CreateChannel(const std::string& address) {
  grpc::ChannelArguments arguments;
  arguments.SetInt(GRPC_ARG_ENABLE_HTTP_PROXY, 0);
  arguments.SetInt(GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS, 100);
  arguments.SetInt(GRPC_ARG_MAX_RECONNECT_BACKOFF_MS, 500);
  return grpc::CreateCustomChannel(address, grpc::InsecureChannelCredentials(), arguments);
}

}  // namespace

ChassisEventClient::ChassisEventClient(const std::string& address)
    : stub_(proto::vehicle::VehicleDataService::NewStub(CreateChannel(address))) {
}

int ChassisEventClient::Stream(const std::function<bool(vehicle::ChassisEvent)>& callback,
                               const std::function<bool()>& keep_running) {
  while (keep_running()) {
    proto::vehicle::SubscribeChassisEventsRequest request;
    request.set_consumer("sentinel");
    grpc::ClientContext context;
    context.set_wait_for_ready(true);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      active_context_ = &context;
    }
    auto reader = stub_->SubscribeChassisEvents(&context, request);
    proto::vehicle::ChassisEvent message;
    while (keep_running() && reader->Read(&message)) {
      if (message.type() != proto::vehicle::CHASSIS_EVENT_TYPE_MOTION_DETECTED ||
          !message.has_motion())
        continue;
      vehicle::ChassisEvent event;
      event.sequence = message.sequence();
      event.timestamp_ms = message.timestamp_ms();
      event.type = vehicle::ChassisEventType::kMotionDetected;
      event.source = message.source();
      event.sensor_id = message.motion().sensor_id();
      event.motion_detected = message.motion().detected();
      callback(std::move(event));
    }
    const grpc::Status status = reader->Finish();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (active_context_ == &context) active_context_ = nullptr;
    }
    if (!keep_running()) return 0;
    if (status.error_code() != grpc::StatusCode::UNAVAILABLE &&
        status.error_code() != grpc::StatusCode::CANCELLED)
      return 1;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  return 0;
}

void ChassisEventClient::Stop() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (active_context_ != nullptr) active_context_->TryCancel();
}

}  // namespace sentinel
}  // namespace cockpit
