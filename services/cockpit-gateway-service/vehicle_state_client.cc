#include "vehicle_state_client.h"

#include "core/logging/Logger.h"
#include "modules/vehicle/VehicleState.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace cockpit {
namespace gateway {
namespace {

vehicle::VehicleState FromProto(const proto::vehicle::VehicleState& message) {
  vehicle::VehicleState state;
  state.timestamp_ms = message.timestamp_ms();
  state.speed_kph = message.speed_kph();
  state.gear = message.gear();
  state.soc_percent = message.soc_percent();
  state.cloud_enabled = message.cloud_enabled();
  state.source = message.source();
  return state;
}

}  // namespace

VehicleStateClient::VehicleStateClient(const std::string& address,
                                       int stream_timeout_ms,
                                       int retry_delay_ms)
    : channel_([&address] {
        grpc::ChannelArguments arguments;
        arguments.SetInt(GRPC_ARG_ENABLE_HTTP_PROXY, 0);
        return grpc::CreateCustomChannel(
            address, grpc::InsecureChannelCredentials(), arguments);
      }()),
      stub_(proto::vehicle::VehicleDataService::NewStub(channel_)),
      stream_timeout_ms_(stream_timeout_ms),
      retry_delay_ms_(retry_delay_ms) {}

int VehicleStateClient::Stream(int sample_count, int max_hz) {
  if (sample_count <= 0) {
    return 0;
  }

  const auto overall_deadline =
      std::chrono::system_clock::now() + std::chrono::milliseconds(stream_timeout_ms_);
  int received = 0;
  std::int64_t last_timestamp_ms = 0;
  grpc::Status last_status;
  while (received < sample_count && std::chrono::system_clock::now() < overall_deadline) {
    grpc::ClientContext context;
    context.set_deadline(overall_deadline);
    context.set_wait_for_ready(true);

    proto::vehicle::SubscribeVehicleStateRequest request;
    request.set_consumer("cockpit-gateway-service");
    request.set_max_hz(max_hz);

    auto reader = stub_->SubscribeVehicleState(&context, request);
    proto::vehicle::VehicleState message;
    while (received < sample_count && reader->Read(&message)) {
      if (message.timestamp_ms() <= last_timestamp_ms) {
        continue;
      }
      const vehicle::VehicleState state = FromProto(message);
      std::cout << "gateway vehicle state: " << state.ToJson() << std::endl;
      last_timestamp_ms = message.timestamp_ms();
      ++received;
    }

    if (received >= sample_count) {
      context.TryCancel();
    }
    last_status = reader->Finish();
    if (received < sample_count && std::chrono::system_clock::now() < overall_deadline) {
      LOG_WARN("vehicle state stream interrupted; retrying grpc_code=" +
               std::to_string(last_status.error_code()));
      std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms_));
    }
  }

  if (received >= sample_count) {
    LOG_INFO("gateway received vehicle states count=" + std::to_string(received));
    return 0;
  }

  LOG_ERROR("vehicle state stream ended received=" + std::to_string(received) +
            " grpc_code=" + std::to_string(last_status.error_code()) +
            " message=" + last_status.error_message());
  return 1;
}

}  // namespace gateway
}  // namespace cockpit
