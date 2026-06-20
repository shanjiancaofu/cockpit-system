#include "core/runtime/ServiceRuntime.h"
#include "vehicle_data_service.h"

int main(int argc, char** argv) {
  auto runtime = cockpit::runtime::ServiceRuntime::Create(argc, argv, "vehicle-data-service");
  cockpit::vehicle::VehicleDataService service(runtime);
  const int result = service.Run();
  runtime.MarkStopped();
  return result;
}
