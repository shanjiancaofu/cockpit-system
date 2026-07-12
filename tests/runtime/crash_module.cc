#include <chrono>
#include <csignal>
#include <thread>

#include "cockpit/navigator/common/module_api.h"

namespace {

int Start(const char*) {
  std::thread([] {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    raise(SIGSEGV);
  }).detach();
  return 0;
}

void Stop() {
}

const CockpitModuleApi kApi{COCKPIT_MODULE_ABI_VERSION, sizeof(CockpitModuleApi), "crash", Start,
                            Stop};

}  // namespace

extern "C" const CockpitModuleApi* CockpitModuleGetApi() {
  return &kApi;
}
