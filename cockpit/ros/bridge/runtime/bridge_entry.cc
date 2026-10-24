#include <memory>

#include "cockpit/exchange/abi/module_api.h"
#include "cockpit/ros/bridge/runtime/bridge_runtime.h"

namespace {

std::unique_ptr<cockpit::bridge::BridgeRuntime> g_bridge;

int Start(const char* config_path) {
  if (config_path == nullptr || g_bridge != nullptr) return 1;
  auto runtime = std::make_unique<cockpit::bridge::BridgeRuntime>();
  if (!runtime->Start(config_path)) return 1;
  g_bridge = std::move(runtime);
  return 0;
}

void Stop() {
  g_bridge.reset();
}
int Poll() {
  return g_bridge == nullptr ? 1 : g_bridge->Poll();
}

const CockpitModuleApi kModuleApi{
    COCKPIT_MODULE_ABI_VERSION, sizeof(CockpitModuleApi), "bridge", Start, Stop, Poll};

}  // namespace

extern "C" const CockpitModuleApi* CockpitModuleGetApi() {
  return &kModuleApi;
}
