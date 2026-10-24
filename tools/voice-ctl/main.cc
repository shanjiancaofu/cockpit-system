#include "voice_ctl.h"

#include "cockpit/exchange/foundation/runtime/process_runtime.h"

int main(int argc, char** argv) {
  const auto runtime = cockpit::runtime::ProcessRuntime::Create(argc, argv, "voice-ctl");
  const int result = cockpit::voice_ctl::ControlVoice(runtime);
  runtime.MarkStopped();
  return result;
}
