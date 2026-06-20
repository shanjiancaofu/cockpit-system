#pragma once

#include "vehicle_state.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <memory>
#include <string>

namespace cockpit {
namespace gateway {

class VehicleStateClient {
 public:
  VehicleStateClient(const std::string& address, int stream_timeout_ms, int retry_delay_ms);

  int Stream(int sample_count, int max_hz);

 private:
  std::shared_ptr<grpc::Channel> channel_;
  std::unique_ptr<proto::vehicle::VehicleDataService::Stub> stub_;
  int stream_timeout_ms_;
  int retry_delay_ms_;
};

}  // namespace gateway
}  // namespace cockpit
