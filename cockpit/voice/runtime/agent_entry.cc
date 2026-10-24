#include <memory>
#include <utility>

#include "cockpit/exchange/abi/module_api.h"
#include "cockpit/voice/runtime/agent_runtime.h"

namespace {

std::unique_ptr<cockpit::agent::AgentRuntime> g_agent;

int Start(const char* config_path) {
  if (config_path == nullptr || g_agent != nullptr) {
    return 1;
  }
  auto agent = std::make_unique<cockpit::agent::AgentRuntime>();
  if (!agent->Start(config_path)) {
    return 1;
  }
  g_agent = std::move(agent);
  return 0;
}

void Stop() {
  g_agent.reset();
}

int Poll() {
  return g_agent == nullptr ? 1 : g_agent->Poll();
}

const CockpitModuleApi kModuleApi{
    COCKPIT_MODULE_ABI_VERSION, sizeof(CockpitModuleApi), "agent", Start, Stop, Poll};

}  // namespace

extern "C" const CockpitModuleApi* CockpitModuleGetApi() {
  return &kModuleApi;
}
