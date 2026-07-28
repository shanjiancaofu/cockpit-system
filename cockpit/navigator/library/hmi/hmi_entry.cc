#include <memory>
#include <utility>

#include "cockpit/library/hmi/hmi_runtime.h"
#include "cockpit/navigator/common/module_api.h"

namespace {

std::unique_ptr<cockpit::hmi::HmiRuntime> g_hmi;

int Start(const char* config_path) {
  if (config_path == nullptr || g_hmi != nullptr) {
    return 1;
  }
  auto hmi = std::make_unique<cockpit::hmi::HmiRuntime>();
  if (!hmi->Start(config_path)) {
    return 1;
  }
  g_hmi = std::move(hmi);
  return 0;
}

void Stop() {
  g_hmi.reset();
}

int Poll() {
  return g_hmi == nullptr ? 1 : g_hmi->Poll();
}

const CockpitModuleApi kModuleApi{
    COCKPIT_MODULE_ABI_VERSION, sizeof(CockpitModuleApi), "hmi", Start, Stop, Poll};

}  // namespace

extern "C" const CockpitModuleApi* CockpitModuleGetApi() {
  return &kModuleApi;
}
