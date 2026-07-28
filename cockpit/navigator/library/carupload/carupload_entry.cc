#include <memory>
#include <utility>

#include "cockpit/library/carupload/carupload_runtime.h"
#include "cockpit/navigator/common/module_api.h"

namespace {

std::unique_ptr<cockpit::carupload::CaruploadRuntime> g_carupload;

int Start(const char* config_path) {
  if (config_path == nullptr || g_carupload != nullptr) {
    return 1;
  }
  auto carupload = std::make_unique<cockpit::carupload::CaruploadRuntime>();
  if (!carupload->Start(config_path)) {
    return 1;
  }
  g_carupload = std::move(carupload);
  return 0;
}

void Stop() {
  g_carupload.reset();
}

int Poll() {
  return g_carupload == nullptr ? 1 : g_carupload->Poll();
}

const CockpitModuleApi kModuleApi{
    COCKPIT_MODULE_ABI_VERSION, sizeof(CockpitModuleApi), "carupload", Start, Stop, Poll};

}  // namespace

extern "C" const CockpitModuleApi* CockpitModuleGetApi() {
  return &kModuleApi;
}
