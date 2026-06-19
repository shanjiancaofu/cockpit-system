#include "common/logging/Logger.h"
#include "common/runtime/ServiceRuntime.h"
#include "common/vehicle/VehicleState.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
  auto runtime = cockpit::runtime::ServiceRuntime::Create(argc, argv, "cockpit-gateway-service");
  const auto& config = runtime.config();
  const int grpc_port = config.GetInt("gateway.grpc_port", 50051);
  const int websocket_port = config.GetInt("gateway.websocket_port", 18080);
  LOG_INFO("gateway listen plan grpc_port=" + std::to_string(grpc_port) +
           " websocket_port=" + std::to_string(websocket_port));

  const auto state = cockpit::vehicle::MakeMockVehicleState(0);
  std::cout << "gateway snapshot: " << state.ToJson() << std::endl;
  LOG_INFO("gateway emitted mock snapshot");
  LOG_WARN("gRPC and WebSocket transports are interface placeholders in this scaffold");
  runtime.MarkStopped();
  return 0;
}
