#include "cockpit/navigator/common/module_api.h"

namespace {
int Start(const char*) {
  return 0;
}
void Stop() {
}
const CockpitModuleApi kApi{COCKPIT_MODULE_ABI_VERSION, sizeof(CockpitModuleApi), "debugger", Start,
                            Stop};
}  // namespace

extern "C" const CockpitModuleApi* CockpitModuleGetApi() {
  return &kApi;
}
