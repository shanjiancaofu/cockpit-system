#include "core/logging/Logger.h"
#include "core/runtime/ServiceRuntime.h"
#include "gateway_grpc_service.h"
#include "vehicle_state_client.h"

#include <string>

int main(int argc, char** argv) {
  auto runtime = cockpit::runtime::ServiceRuntime::Create(argc, argv, "cockpit-gateway-service");
  const auto& config = runtime.config().services().gateway;
  const std::string& grpc_address = config.grpc.listen_address;
  const std::string& websocket_address = config.websocket.listen_address;
  const std::string& vehicle_data_address = config.vehicle_data_address;
  const int samples = runtime.args().GetInt("samples", 0);
  const int max_hz = runtime.args().GetInt("max-hz", 10);
  LOG_INFO("gateway listen plan grpc_address=" + grpc_address +
           " websocket_address=" + websocket_address);

  cockpit::gateway::GatewayGrpcService gateway_service;
  if (!gateway_service.Start(grpc_address)) {
    runtime.MarkStopped();
    return 1;
  }

  cockpit::gateway::VehicleStateClient client(
      vehicle_data_address, config.stream_timeout_ms, config.retry_delay_ms);
  const int result = client.Stream(
      samples, max_hz, [&gateway_service](const cockpit::proto::vehicle::VehicleState& state) {
        gateway_service.PublishVehicleState(state);
      }, [&runtime] { return !runtime.ShouldStop(); });
  gateway_service.Shutdown();
  runtime.MarkStopped();
  return result;
}
