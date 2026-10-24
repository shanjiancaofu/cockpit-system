#pragma once

#include <grpcpp/grpcpp.h>

#include <functional>
#include <memory>
#include <string>

#include "vehicle_state.grpc.pb.h"

namespace cockpit {
namespace gateway {

class VehicleStateClient {
 public:
  using StateHandler = std::function<void(const proto::vehicle::VehicleState&)>;
  using ContinueHandler = std::function<bool()>;

  VehicleStateClient(const std::string& address, int stream_timeout_ms, int retry_delay_ms);

  int Stream(int sample_count, int max_hz, const StateHandler& handler,
             const ContinueHandler& should_continue);

 private:
  std::shared_ptr<grpc::Channel> channel_;
  std::unique_ptr<proto::vehicle::VehicleDataService::Stub> stub_;
  int stream_timeout_ms_;
  int retry_delay_ms_;
};

}  // namespace gateway
}  // namespace cockpit
