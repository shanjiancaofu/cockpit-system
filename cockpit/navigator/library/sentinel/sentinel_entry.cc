#include <memory>
#include <utility>

#include "cockpit/library/sentinel/sentinel_runtime.h"
#include "cockpit/navigator/common/module_api.h"

namespace {

std::unique_ptr<cockpit::sentinel::SentinelRuntime> g_sentinel;

int Start(const char* config_path) {
  if (config_path == nullptr || g_sentinel != nullptr) return 1;
  auto sentinel = std::make_unique<cockpit::sentinel::SentinelRuntime>();
  if (!sentinel->Start(config_path)) return 1;
  g_sentinel = std::move(sentinel);
  return 0;
}

void Stop() { g_sentinel.reset(); }
int Poll() { return g_sentinel == nullptr ? 1 : g_sentinel->Poll(); }

const CockpitModuleApi kModuleApi{
    COCKPIT_MODULE_ABI_VERSION, sizeof(CockpitModuleApi), "sentinel", Start, Stop, Poll};

}  // namespace

extern "C" const CockpitModuleApi* CockpitModuleGetApi() { return &kModuleApi; }
