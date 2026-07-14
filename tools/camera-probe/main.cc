#include "camera_probe.h"

#include "cockpit/core/runtime/process_runtime.h"

int main(int argc, char** argv) {
  const auto runtime = cockpit::runtime::ProcessRuntime::Create(argc, argv, "camera-probe");
  const int result = cockpit::camera_probe::ProbeCamera(runtime);
  runtime.MarkStopped();
  return result;
}
