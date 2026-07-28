#include "cockpit/navigator/common/module_api.h"

namespace {

int Start(const char*) {
  return 1;
}

void Stop() {
}

const CockpitModuleApi kModuleApi{
    COCKPIT_MODULE_ABI_VERSION, sizeof(CockpitModuleApi), "debugger", Start, Stop, nullptr};

}  // namespace

extern "C" const CockpitModuleApi* CockpitModuleGetApi() {
  return &kModuleApi;
}
