#include "tools/diagnostics/cli_output.h"

#include <iostream>
#include <sstream>
#include <string>

#include "camera.pb.h"
#include "cockpit/core/json/json.h"

namespace {

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  cockpit::diagnostics::OutputFormat format;
  std::string error;
  cockpit::proto::camera::CameraStatus status;
  status.set_state(cockpit::proto::camera::CAMERA_PREVIEW_STATE_RUNNING);
  status.set_device("synthetic://camera0");
  status.set_frames_received(7);
  std::ostringstream output;
  const bool result =
      Check(cockpit::diagnostics::ParseOutputFormat("json", &format, &error),
            "JSON output format was rejected") &&
      Check(format == cockpit::diagnostics::OutputFormat::kJson,
            "JSON output format parsed incorrectly") &&
      Check(!cockpit::diagnostics::ParseOutputFormat("yaml", &format, &error),
            "unsupported output format was accepted") &&
      Check(cockpit::diagnostics::WriteJson(status, &output, &error),
            "protobuf JSON serialization failed") &&
      Check(cockpit::json::IsValidValue(output.str()), "CLI output is not valid JSON") &&
      Check(output.str().find("\"frames_received\":\"7\"") != std::string::npos,
            "CLI JSON omitted uint64 metric") &&
      Check(
          cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kSuccess) == 0 &&
              cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kOperationFailed) == 1 &&
              cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kInvalidArguments) == 2 &&
              cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kUnhealthy) == 3,
          "CLI exit code contract mismatch");
  return result ? 0 : 1;
}
