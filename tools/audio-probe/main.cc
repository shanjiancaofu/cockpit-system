#include "audio_probe.h"

#include "cockpit/core/runtime/process_runtime.h"

int main(int argc, char** argv) {
  const auto runtime = cockpit::runtime::ProcessRuntime::Create(argc, argv, "audio-probe");
  const int result = cockpit::audio_probe::ProbeAudio(runtime);
  runtime.MarkStopped();
  return result;
}
