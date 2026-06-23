#include "topic_grpc_subscriber.h"

#include <grpcpp/grpcpp.h>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <utility>

#include "topic_text.h"

#include "gateway.grpc.pb.h"

namespace cockpit {
namespace topic {
namespace {

std::string VehicleStateJson(const proto::vehicle::VehicleState& state) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(1) << "{\"timestamp_ms\":" << state.timestamp_ms()
      << ",\"speed_kph\":" << state.speed_kph() << ",\"gear\":" << state.gear()
      << ",\"soc_percent\":" << state.soc_percent()
      << ",\"cloud_enabled\":" << (state.cloud_enabled() ? "true" : "false") << ",\"source\":\""
      << EscapeJson(state.source()) << "\"}";
  return out.str();
}

TopicSample ToSample(const proto::gateway::CockpitEvent& event) {
  TopicSample sample;
  sample.timestamp_ms = event.timestamp_ms();
  std::ostringstream out;
  out << "{\"timestamp_ms\":" << event.timestamp_ms()
      << ",\"topic\":\"/vehicle/state\",\"payload\":" << VehicleStateJson(event.vehicle_state())
      << '}';
  sample.json = out.str();
  return sample;
}

}  // namespace

TopicGrpcSubscriber::TopicGrpcSubscriber(std::string address, int timeout_ms)
    : address_(std::move(address)), timeout_ms_(timeout_ms) {
}

int TopicGrpcSubscriber::Stream(const std::string& topic, int count, int max_hz,
                                const SampleHandler& handler) const {
  if (topic != "/vehicle/state") {
    std::cerr << "gRPC backend does not expose topic: " << topic << '\n';
    return 2;
  }

  grpc::ChannelArguments arguments;
  arguments.SetInt(GRPC_ARG_ENABLE_HTTP_PROXY, 0);
  auto channel = grpc::CreateCustomChannel(address_, grpc::InsecureChannelCredentials(), arguments);
  auto stub = proto::gateway::CockpitGateway::NewStub(channel);

  proto::gateway::SubscribeCockpitEventsRequest request;
  request.set_client_id("topic");
  request.set_max_hz(std::clamp(max_hz, 1, 100));
  const auto deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(timeout_ms_);
  int received = 0;
  grpc::Status last_status;
  while (count <= 0 || (received < count && std::chrono::system_clock::now() < deadline)) {
    grpc::ClientContext context;
    context.set_wait_for_ready(true);
    if (count > 0) {
      context.set_deadline(deadline);
    }
    auto reader = stub->SubscribeCockpitEvents(&context, request);
    proto::gateway::CockpitEvent event;
    while ((count <= 0 || received < count) && reader->Read(&event)) {
      if (!event.has_vehicle_state()) {
        continue;
      }
      handler(ToSample(event));
      ++received;
    }
    if (count > 0 && received >= count) {
      context.TryCancel();
    }
    last_status = reader->Finish();
    if (count > 0 && received >= count) {
      return 0;
    }
    if (count > 0 && std::chrono::system_clock::now() >= deadline) {
      break;
    }
    if (last_status.error_code() != grpc::StatusCode::UNAVAILABLE &&
        last_status.error_code() != grpc::StatusCode::UNKNOWN &&
        last_status.error_code() != grpc::StatusCode::CANCELLED && !last_status.ok()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::cerr << "gRPC topic stream ended after " << received
            << " messages: code=" << last_status.error_code()
            << " message=" << last_status.error_message() << '\n';
  return 1;
}

}  // namespace topic
}  // namespace cockpit
