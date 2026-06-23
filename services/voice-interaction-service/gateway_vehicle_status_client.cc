#include "gateway_vehicle_status_client.h"

#include <grpcpp/grpcpp.h>

#include <chrono>

#include "common.pb.h"

namespace cockpit {
namespace voice {

GatewayVehicleStatusClient::GatewayVehicleStatusClient(const std::string& address)
    : stub_([&address] {
        grpc::ChannelArguments arguments;
        arguments.SetInt(GRPC_ARG_ENABLE_HTTP_PROXY, 0);
        return proto::gateway::CockpitGateway::NewStub(
            grpc::CreateCustomChannel(address, grpc::InsecureChannelCredentials(), arguments));
      }()) {
}

bool GatewayVehicleStatusClient::GetLatest(VehicleStatusSnapshot* status, std::string* error) {
  proto::common::Empty request;
  proto::vehicle::VehicleState response;
  grpc::ClientContext context;
  context.set_wait_for_ready(true);
  context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(500));
  const grpc::Status rpc_status = stub_->GetLatestVehicleState(&context, request, &response);
  if (!rpc_status.ok()) {
    if (error != nullptr) {
      *error = "Vehicle status unavailable: " + rpc_status.error_message();
    }
    return false;
  }
  if (status != nullptr) {
    status->timestamp_ms = response.timestamp_ms();
    status->speed_kph = response.speed_kph();
    status->gear = response.gear();
    status->soc_percent = response.soc_percent();
    status->source = response.source();
  }
  return true;
}

}  // namespace voice
}  // namespace cockpit
