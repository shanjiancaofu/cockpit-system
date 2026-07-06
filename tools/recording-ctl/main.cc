#include <iostream>
#include <string>

#include "recording_control_client.h"

#include "cockpit/core/runtime/ServiceRuntime.h"

namespace {

const char* StateName(cockpit::proto::recording::RecordingState state) {
  switch (state) {
    case cockpit::proto::recording::RECORDING_STATE_IDLE:
      return "idle";
    case cockpit::proto::recording::RECORDING_STATE_RECORDING:
      return "recording";
    case cockpit::proto::recording::RECORDING_STATE_FAULTED:
      return "faulted";
    case cockpit::proto::recording::RECORDING_STATE_UNSPECIFIED:
    default:
      return "unspecified";
  }
}

void PrintStatus(const cockpit::proto::recording::RecordingStatus& status) {
  std::cout << "state: " << StateName(status.state()) << '\n'
            << "session id: " << status.session_id() << '\n'
            << "directory: " << status.directory() << '\n'
            << "trigger: " << status.trigger() << '\n'
            << "messages written: " << status.messages_written() << '\n'
            << "started at ms: " << status.started_at_ms() << '\n'
            << "stopped at ms: " << status.stopped_at_ms() << '\n'
            << "last message timestamp ms: " << status.last_message_timestamp_ms() << '\n';
  if (!status.last_error().empty()) {
    std::cout << "last error: " << status.last_error() << '\n';
  }
}

int Finish(const cockpit::runtime::ServiceRuntime& runtime, int result) {
  runtime.MarkStopped();
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  const auto runtime = cockpit::runtime::ServiceRuntime::Create(argc, argv, "recording-ctl");
  cockpit::recording::RecordingControlClient client(
      runtime.config().services().recording.grpc.listen_address);
  cockpit::proto::recording::RecordingStatus status;
  std::string error;
  bool ok = false;
  if (runtime.args().HasFlag("start")) {
    ok = client.Start(runtime.args().GetString("trigger", "manual"), &status, &error);
  } else if (runtime.args().HasFlag("stop")) {
    ok = client.Stop(&status, &error);
  } else {
    ok = client.GetStatus(&status, &error);
  }
  if (!ok) {
    std::cerr << (error.empty() ? "recording control request failed" : error) << '\n';
    return Finish(runtime, 1);
  }
  PrintStatus(status);
  return Finish(runtime, 0);
}
