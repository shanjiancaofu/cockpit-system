#include <memory>
#include <utility>

#include "cockpit/library/recording/recording_runtime.h"
#include "cockpit/navigator/common/module_api.h"

namespace {

std::unique_ptr<cockpit::recording::RecordingRuntime> g_recording;

int Start(const char* config_path) {
  if (config_path == nullptr || g_recording != nullptr) {
    return 1;
  }
  auto recording = std::make_unique<cockpit::recording::RecordingRuntime>();
  if (!recording->Start(config_path)) {
    return 1;
  }
  g_recording = std::move(recording);
  return 0;
}

void Stop() {
  g_recording.reset();
}

int Poll() {
  return g_recording == nullptr ? 1 : g_recording->Poll();
}

const CockpitModuleApi kModuleApi{
    COCKPIT_MODULE_ABI_VERSION, sizeof(CockpitModuleApi), "recording", Start, Stop, Poll};

}  // namespace

extern "C" const CockpitModuleApi* CockpitModuleGetApi() {
  return &kModuleApi;
}
