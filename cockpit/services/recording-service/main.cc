#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

#include "cockpit/core/build/build_info.h"
#include "cockpit/core/logging/logger.h"
#include "cockpit/core/runtime/service_runtime.h"
#include "cockpit/modules/recording/recording_session.h"
#include "cockpit/services/recording-service/recording_grpc_service.h"
#include "cockpit/services/recording-service/recording_service.h"
#include "cockpit/services/recording-service/vehicle_state_subscriber.h"

namespace {

std::string ConfigChecksum(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return "unavailable";
  }
  std::uint64_t hash = 1469598103934665603ULL;
  char character = 0;
  while (input.get(character)) {
    hash ^= static_cast<unsigned char>(character);
    hash *= 1099511628211ULL;
  }
  std::ostringstream output;
  output << "fnv1a64:" << std::hex << std::setw(16) << std::setfill('0') << hash;
  return output.str();
}

}  // namespace

int main(int argc, char** argv) {
  auto runtime = cockpit::runtime::ServiceRuntime::Create(argc, argv, "recording-service");
  const auto& config = runtime.config().services().recording;
  std::filesystem::path recording_directory(
      runtime.args().GetString("directory", config.directory));
  if (recording_directory.is_relative()) {
    recording_directory =
        std::filesystem::path(runtime.config().paths().data_dir) / recording_directory;
  }

  std::string recovery_error;
  const std::size_t recovered = cockpit::recording::RecordingSession::RecoverInterrupted(
      recording_directory, &recovery_error);
  if (!recovery_error.empty()) {
    LOG_WARN("recover interrupted recordings failed: " + recovery_error);
  } else if (recovered > 0) {
    LOG_WARN("recovered interrupted recordings count=" + std::to_string(recovered));
  }

  cockpit::recording::RecordingMetadata metadata;
  const auto build_info = cockpit::build::GetBuildInfo();
  metadata.config_path = runtime.config_path();
  metadata.config_checksum = ConfigChecksum(runtime.config_path());
  metadata.git_commit = build_info.git_commit;
  metadata.git_dirty = build_info.git_dirty;
  metadata.build_type = build_info.build_type.empty() ? "unknown" : build_info.build_type;
  metadata.binary_version = build_info.version;
  metadata.sources = {"vehicle_state",     "generic_events", "data_files",    "camera_status",
                      "camera_frame_meta", "camera_photo",   "voice_response"};
  cockpit::recording::RecordingService recording_service(
      recording_directory, runtime.config().system().vehicle_id,
      {static_cast<std::size_t>(config.max_sessions), config.max_total_bytes}, metadata);
  std::string initialization_error;
  if (!recording_service.Initialize(&initialization_error)) {
    LOG_ERROR("initialize recording catalog failed: " + initialization_error);
    runtime.MarkStopped();
    return 1;
  }
  if (config.auto_start) {
    std::string error;
    if (!recording_service.Start("auto_start", &error)) {
      LOG_ERROR("auto-start recording failed: " + error);
      runtime.MarkStopped();
      return 1;
    }
  }

  cockpit::recording::RecordingGrpcService grpc_service(recording_service);
  if (!grpc_service.Listen(config.grpc.listen_address)) {
    runtime.MarkStopped();
    return 1;
  }

  cockpit::recording::VehicleStateSubscriber subscriber(
      config.vehicle_data_address, config.stream_timeout_ms, config.retry_delay_ms);
  const int result = subscriber.Stream(
      [&recording_service](const cockpit::proto::vehicle::VehicleState& state) {
        recording_service.HandleVehicleState(state);
      },
      [&runtime] {
        return !runtime.ShouldStop();
      });

  std::string stop_error;
  if (!recording_service.Stop(&stop_error) && !stop_error.empty()) {
    LOG_ERROR("stop recording failed: " + stop_error);
  }
  grpc_service.Shutdown();
  runtime.MarkStopped();
  return result;
}
