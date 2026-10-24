#include "cockpit/exchange/abi/module_api.h"

namespace {

int Start(const char*) {
  return 1;
}

void Stop() {
}

const CockpitModuleApi kModuleApi{
    COCKPIT_MODULE_ABI_VERSION, sizeof(CockpitModuleApi), "watchdog", Start, Stop, nullptr};

}  // namespace

extern "C" const CockpitModuleApi* CockpitModuleGetApi() {
  return &kModuleApi;
}
