#include "camera_preview_probe.h"

#include "cockpit/core/runtime/process_runtime.h"

int main(int argc, char** argv) {
  const auto runtime = cockpit::runtime::ProcessRuntime::Create(argc, argv, "camera-preview-probe");
  const int result = cockpit::camera_preview_probe::ProbeCameraPreview(runtime);
  runtime.MarkStopped();
  return result;
}
