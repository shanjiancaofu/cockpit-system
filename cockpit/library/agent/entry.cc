#include "cockpit/library/agent/agent_runtime.h"
#include "cockpit/navigator/common/module_api.h"

namespace {

cockpit::agent::AgentRuntime runtime;

int Start(const char* config_path) {
  return runtime.Start(config_path) ? 0 : 1;
}

void Stop() {
  runtime.Stop();
}

int Poll() {
  return runtime.Poll();
}

const CockpitModuleApi kApi{
    COCKPIT_MODULE_ABI_VERSION, sizeof(CockpitModuleApi), "agent", Start, Stop, Poll};

}  // namespace

extern "C" const CockpitModuleApi* CockpitModuleGetApi() {
  return &kApi;
}
