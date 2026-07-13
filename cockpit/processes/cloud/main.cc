#include "cockpit/core/runtime/process_runtime.h"
#include "cockpit/library/carupload/carupload_runtime.h"

int main(int argc, char** argv) {
  auto runtime = cockpit::runtime::ProcessRuntime::Create(argc, argv, "cloud-uplink-service");
  cockpit::carupload::CaruploadRuntime carupload;
  const bool started = carupload.Start(runtime.config_path());
  carupload.Stop();
  runtime.MarkStopped();
  return started ? 0 : 1;
}
