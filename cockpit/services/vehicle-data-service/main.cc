#include "vehicle_data_service.h"
#include "vehicle_grpc_service.h"

#include "cockpit/core/runtime/ServiceRuntime.h"

int main(int argc, char** argv) {
  auto runtime = cockpit::runtime::ServiceRuntime::Create(argc, argv, "vehicle-data-service");
  cockpit::vehicle::VehicleGrpcService grpc_service;
  const std::string& grpc_address = runtime.config().services().vehicle_data.grpc.listen_address;
  if (!grpc_service.Start(grpc_address)) {
    runtime.MarkStopped();
    return 1;
  }

  cockpit::vehicle::VehicleDataService service(
      runtime, [&grpc_service](const cockpit::vehicle::VehicleState& state) {
        grpc_service.Publish(state);
      });
  const int result = service.Run();
  grpc_service.Shutdown();
  runtime.MarkStopped();
  return result;
}
