#include <memory>
#include <utility>

#include "cockpit/library/driver/audio/audio_runtime.h"
#include "cockpit/navigator/common/module_api.h"

namespace {

std::unique_ptr<cockpit::audio::AudioRuntime> g_audio;

int Start(const char* config_path) {
  if (config_path == nullptr || g_audio != nullptr) {
    return 1;
  }
  auto audio = std::make_unique<cockpit::audio::AudioRuntime>();
  if (!audio->Start(config_path)) {
    return 1;
  }
  g_audio = std::move(audio);
  return 0;
}

void Stop() {
  g_audio.reset();
}

int Poll() {
  return g_audio == nullptr ? 1 : g_audio->Poll();
}

const CockpitModuleApi kModuleApi{
    COCKPIT_MODULE_ABI_VERSION, sizeof(CockpitModuleApi), "audio_driver", Start, Stop, Poll};

}  // namespace

extern "C" const CockpitModuleApi* CockpitModuleGetApi() {
  return &kModuleApi;
}
