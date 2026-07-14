#include "camera_ctl.h"

#include "cockpit/core/runtime/process_runtime.h"

int main(int argc, char** argv) {
  const auto runtime = cockpit::runtime::ProcessRuntime::Create(argc, argv, "camera-ctl");
  const int result = cockpit::camera_ctl::ControlCamera(runtime);
  runtime.MarkStopped();
  return result;
}
