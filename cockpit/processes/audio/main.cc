#include <chrono>
#include <thread>

#include "cockpit/core/runtime/process_runtime.h"
#include "cockpit/library/driver/audio/audio_runtime.h"

int main(int argc, char** argv) {
  auto runtime = cockpit::runtime::ProcessRuntime::Create(argc, argv, "audio-service");
  cockpit::audio::AudioRuntime audio;
  if (!audio.Start(runtime.config_path(), runtime.args().GetString("output-device", ""))) {
    runtime.MarkStopped();
    return 1;
  }
  while (!runtime.ShouldStop() && audio.Poll() == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  const int result = runtime.ShouldStop() ? 0 : audio.Poll();
  audio.Stop();
  runtime.MarkStopped();
  return result;
}
