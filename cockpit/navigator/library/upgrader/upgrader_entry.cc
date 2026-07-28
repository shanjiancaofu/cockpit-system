#include <memory>
#include <utility>

#include "cockpit/library/upgrader/upgrader_runtime.h"
#include "cockpit/navigator/common/module_api.h"

namespace {

std::unique_ptr<cockpit::upgrader::UpgraderRuntime> g_upgrader;

int Start(const char* config_path) {
  if (config_path == nullptr || g_upgrader != nullptr) {
    return 1;
  }
  auto upgrader = std::make_unique<cockpit::upgrader::UpgraderRuntime>();
  if (!upgrader->Start(config_path)) {
    return 1;
  }
  g_upgrader = std::move(upgrader);
  return 0;
}

void Stop() {
  g_upgrader.reset();
}

int Poll() {
  return g_upgrader == nullptr ? 1 : g_upgrader->Poll();
}

const CockpitModuleApi kModuleApi{
    COCKPIT_MODULE_ABI_VERSION, sizeof(CockpitModuleApi), "upgrader", Start, Stop, Poll};

}  // namespace

extern "C" const CockpitModuleApi* CockpitModuleGetApi() {
  return &kModuleApi;
}
