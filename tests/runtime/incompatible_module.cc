#include "cockpit/navigator/common/module_api.h"

namespace {

int Start(const char*) {
  return 0;
}

void Stop() {
}

const CockpitModuleApi kApi{
    COCKPIT_MODULE_ABI_VERSION + 1, sizeof(CockpitModuleApi), "incompatible", Start, Stop, nullptr};

}  // namespace

extern "C" const CockpitModuleApi* CockpitModuleGetApi() {
  return &kApi;
}
