#include "cockpit/library/recording/recording_runtime.h"
#include "cockpit/navigator/common/module_api.h"

namespace {

cockpit::recording::RecordingRuntime runtime;

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
    COCKPIT_MODULE_ABI_VERSION, sizeof(CockpitModuleApi), "recording", Start, Stop, Poll};

}  // namespace

extern "C" const CockpitModuleApi* CockpitModuleGetApi() {
  return &kApi;
}
