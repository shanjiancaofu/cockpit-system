#include "recording_ctl.h"

#include "cockpit/exchange/foundation/runtime/process_runtime.h"

int main(int argc, char** argv) {
  const auto runtime = cockpit::runtime::ProcessRuntime::Create(argc, argv, "recording-ctl");
  const int result = cockpit::recording_ctl::ControlRecording(runtime);
  runtime.MarkStopped();
  return result;
}
