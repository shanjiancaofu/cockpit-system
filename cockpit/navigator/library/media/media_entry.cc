#include <memory>
#include <utility>

#include "cockpit/library/media/media_runtime.h"
#include "cockpit/navigator/common/module_api.h"

namespace {

std::unique_ptr<cockpit::media::MediaRuntime> g_media;

int Start(const char* config_path) {
  if (config_path == nullptr || g_media != nullptr) {
    return 1;
  }
  auto media = std::make_unique<cockpit::media::MediaRuntime>();
  if (!media->Start(config_path)) {
    return 1;
  }
  g_media = std::move(media);
  return 0;
}

void Stop() {
  g_media.reset();
}

int Poll() {
  return g_media == nullptr ? 1 : g_media->Poll();
}

const CockpitModuleApi kModuleApi{
    COCKPIT_MODULE_ABI_VERSION, sizeof(CockpitModuleApi), "media", Start, Stop, Poll};

}  // namespace

extern "C" const CockpitModuleApi* CockpitModuleGetApi() {
  return &kModuleApi;
}
