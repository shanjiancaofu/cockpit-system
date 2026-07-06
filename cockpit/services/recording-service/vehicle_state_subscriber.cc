#include "cockpit/services/recording-service/vehicle_state_subscriber.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "cockpit/core/logging/Logger.h"

namespace cockpit {
namespace recording {

VehicleStateSubscriber::VehicleStateSubscriber(const std::string& address, int stream_timeout_ms,
                                               int retry_delay_ms)
    : stub_(proto::vehicle::VehicleDataService::NewStub([&address] {
        grpc::ChannelArguments arguments;
        arguments.SetInt(GRPC_ARG_ENABLE_HTTP_PROXY, 0);
        return grpc::CreateCustomChannel(address, grpc::InsecureChannelCredentials(), arguments);
      }())),
      stream_timeout_ms_(stream_timeout_ms),
      retry_delay_ms_(retry_delay_ms) {
}

int VehicleStateSubscriber::Stream(const StateHandler& handler,
                                   const ContinueHandler& should_continue) {
  std::int64_t last_timestamp_ms = 0;
  while (should_continue()) {
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() +
                         std::chrono::milliseconds(stream_timeout_ms_));
    context.set_wait_for_ready(true);

    proto::vehicle::SubscribeVehicleStateRequest request;
    request.set_consumer("recording-service");
    request.set_max_hz(0);
    auto reader = stub_->SubscribeVehicleState(&context, request);

    std::atomic_bool stream_finished{false};
    std::thread stop_watcher([&context, &should_continue, &stream_finished] {
      while (!stream_finished.load() && should_continue()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
      if (!should_continue()) {
        context.TryCancel();
      }
    });

    proto::vehicle::VehicleState message;
    while (reader->Read(&message)) {
      if (!should_continue()) {
        context.TryCancel();
        break;
      }
      if (message.timestamp_ms() <= last_timestamp_ms) {
        continue;
      }
      handler(message);
      last_timestamp_ms = message.timestamp_ms();
    }

    const grpc::Status status = reader->Finish();
    stream_finished.store(true);
    stop_watcher.join();
    if (!should_continue()) {
      break;
    }
    LOG_WARN("recording vehicle stream interrupted; retrying grpc_code=" +
             std::to_string(status.error_code()));
    std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms_));
  }
  return 0;
}

}  // namespace recording
}  // namespace cockpit
