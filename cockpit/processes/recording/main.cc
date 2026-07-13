#include <chrono>
#include <thread>

#include "cockpit/core/runtime/process_runtime.h"
#include "cockpit/library/recording/recording_runtime.h"

int main(int argc, char** argv) {
  auto runtime = cockpit::runtime::ProcessRuntime::Create(argc, argv, "recording-service");
  cockpit::recording::RecordingRuntime recording;
  if (!recording.Start(runtime.config_path(), runtime.args().GetString("directory", ""))) {
    runtime.MarkStopped();
    return 1;
  }
  while (!runtime.ShouldStop() && recording.Poll() == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  const int result = runtime.ShouldStop() ? 0 : recording.Poll();
  recording.Stop();
  runtime.MarkStopped();
  return result;
}
