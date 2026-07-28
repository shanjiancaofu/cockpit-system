#include <memory>
#include <utility>

#include "cockpit/library/driver/camera/camera_runtime.h"
#include "cockpit/navigator/common/module_api.h"

namespace {

std::unique_ptr<cockpit::camera::CameraRuntime> g_camera;

int Start(const char* config_path) {
  if (config_path == nullptr || g_camera != nullptr) {
    return 1;
  }
  auto camera = std::make_unique<cockpit::camera::CameraRuntime>();
  if (!camera->Start(config_path)) {
    return 1;
  }
  g_camera = std::move(camera);
  return 0;
}

void Stop() {
  g_camera.reset();
}

int Poll() {
  return g_camera == nullptr ? 1 : g_camera->Poll();
}

const CockpitModuleApi kModuleApi{
    COCKPIT_MODULE_ABI_VERSION, sizeof(CockpitModuleApi), "camera_driver", Start, Stop, Poll};

}  // namespace

extern "C" const CockpitModuleApi* CockpitModuleGetApi() {
  return &kModuleApi;
}
