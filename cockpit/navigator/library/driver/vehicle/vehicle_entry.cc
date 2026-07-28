#include <memory>
#include <utility>

#include "cockpit/library/driver/vehicle/vehicle_runtime.h"
#include "cockpit/navigator/common/module_api.h"

namespace {

std::unique_ptr<cockpit::vehicle::VehicleRuntime> g_vehicle;

int Start(const char* config_path) {
  if (config_path == nullptr || g_vehicle != nullptr) {
    return 1;
  }
  auto vehicle = std::make_unique<cockpit::vehicle::VehicleRuntime>();
  if (!vehicle->Start(config_path)) {
    return 1;
  }
  g_vehicle = std::move(vehicle);
  return 0;
}

void Stop() {
  g_vehicle.reset();
}

int Poll() {
  return g_vehicle == nullptr ? 1 : g_vehicle->Poll();
}

const CockpitModuleApi kModuleApi{
    COCKPIT_MODULE_ABI_VERSION, sizeof(CockpitModuleApi), "vehicle_driver", Start, Stop, Poll};

}  // namespace

extern "C" const CockpitModuleApi* CockpitModuleGetApi() {
  return &kModuleApi;
}
