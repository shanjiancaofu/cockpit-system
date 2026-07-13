#include <chrono>
#include <thread>

#include "cockpit/core/runtime/process_runtime.h"
#include "cockpit/library/driver/camera/camera_runtime.h"

int main(int argc, char** argv) {
  auto runtime = cockpit::runtime::ProcessRuntime::Create(argc, argv, "camera-service");
  cockpit::camera::CameraRuntime camera;
  if (!camera.Start(runtime.config_path())) {
    runtime.MarkStopped();
    return 1;
  }
  while (!runtime.ShouldStop() && camera.Poll() == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  const int result = runtime.ShouldStop() ? 0 : camera.Poll();
  camera.Stop();
  runtime.MarkStopped();
  return result;
}
