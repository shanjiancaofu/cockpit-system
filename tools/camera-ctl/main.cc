#include <string>

#include "camera_ctl.h"
#include "camera_tuning_ctl.h"

#include "cockpit/exchange/foundation/runtime/process_runtime.h"

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "tuning")
    return cockpit::camera_ctl::ControlNativeTuning(argc - 1, argv + 1);
  if (argc > 1 && std::string(argv[1]) == "tuning-service")
    return cockpit::camera_ctl::ControlServiceTuning(argc - 1, argv + 1);
  const auto runtime = cockpit::runtime::ProcessRuntime::Create(argc, argv, "camera-ctl");
  const int result = cockpit::camera_ctl::ControlCamera(runtime);
  runtime.MarkStopped();
  return result;
}
