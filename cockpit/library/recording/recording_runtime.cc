#include "cockpit/library/recording/recording_runtime.h"

#include <atomic>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#include "cockpit/core/build/build_info.h"
#include "cockpit/core/config/system_config.h"
#include "cockpit/core/logging/logger.h"
#include "cockpit/library/recording/recording_grpc_service.h"
#include "cockpit/library/recording/recording_service.h"
#include "cockpit/library/recording/vehicle_state_subscriber.h"
#include "cockpit/modules/recording/recording_session.h"

namespace cockpit {
namespace recording {
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

class RecordingRuntime::Impl {
 public:
  Impl(std::filesystem::path directory, std::string vehicle_id, RecordingRetentionPolicy retention,
       RecordingMetadata metadata, std::string vehicle_address, int stream_timeout_ms,
       int retry_delay_ms)
      : service(std::move(directory), std::move(vehicle_id), retention, std::move(metadata)),
        grpc(service),
        subscriber(std::move(vehicle_address), stream_timeout_ms, retry_delay_ms) {
  }

  RecordingService service;
  RecordingGrpcService grpc;
  VehicleStateSubscriber subscriber;
  std::thread worker;
  std::atomic_bool stopping{false};
  std::atomic_bool running{false};
  std::atomic_int result{0};
};

RecordingRuntime::RecordingRuntime() = default;

RecordingRuntime::~RecordingRuntime() {
  Stop();
}

bool RecordingRuntime::Start(const std::string& config_path,
                             const std::string& directory_override) {
  if (impl_ != nullptr) {
    return false;
  }

  try {
    const auto system_config = config::SystemConfig::LoadFromFile(config_path);
    const auto& recording_config = system_config.services().recording;
    logging::InitLogger("recording", system_config.paths().log_dir,
                        logging::ParseLevel(system_config.logging().level),
                        system_config.logging().max_bytes, system_config.logging().mirror_stderr);

    std::filesystem::path directory(directory_override.empty() ? recording_config.directory
                                                               : directory_override);
    if (directory.is_relative()) {
      directory = std::filesystem::path(system_config.paths().data_dir) / directory;
    }
    std::string recovery_error;
    const std::size_t recovered = RecordingSession::RecoverInterrupted(directory, &recovery_error);
    if (!recovery_error.empty()) {
      LOG_WARN("recover interrupted recordings failed: " + recovery_error);
    } else if (recovered > 0) {
      LOG_WARN("recovered interrupted recordings count=" + std::to_string(recovered));
    }

    RecordingMetadata metadata;
    const auto build_info = build::GetBuildInfo();
    metadata.config_path = config_path;
    metadata.config_checksum = ConfigChecksum(config_path);
    metadata.git_commit = build_info.git_commit;
    metadata.git_dirty = build_info.git_dirty;
    metadata.build_type = build_info.build_type.empty() ? "unknown" : build_info.build_type;
    metadata.binary_version = build_info.version;
    metadata.sources = {"vehicle_state",     "generic_events", "data_files",    "camera_status",
                        "camera_frame_meta", "camera_photo",   "voice_response"};

    impl_ = std::make_unique<Impl>(
        directory, system_config.system().vehicle_id,
        RecordingRetentionPolicy{static_cast<std::size_t>(recording_config.max_sessions),
                                 recording_config.max_total_bytes},
        std::move(metadata), recording_config.vehicle_data_address,
        recording_config.stream_timeout_ms, recording_config.retry_delay_ms);
    std::string error;
    if (!impl_->service.Initialize(&error)) {
      LOG_ERROR("initialize recording catalog failed: " + error);
      impl_.reset();
      return false;
    }
    if (recording_config.auto_start && !impl_->service.Start("auto_start", &error)) {
      LOG_ERROR("auto-start recording failed: " + error);
      impl_.reset();
      return false;
    }
    if (!impl_->grpc.Listen(recording_config.grpc.listen_address)) {
      impl_.reset();
      return false;
    }

    impl_->running.store(true);
    impl_->worker = std::thread([this] {
      int result = impl_->subscriber.Stream(
          [this](const proto::vehicle::VehicleState& state) {
            impl_->service.HandleVehicleState(state);
          },
          [this] {
            return !impl_->stopping.load();
          });
      if (!impl_->stopping.load() && result == 0) {
        result = 1;
      }
      impl_->result.store(result);
      impl_->running.store(false);
    });
    return true;
  } catch (const std::exception& error) {
    LOG_ERROR("failed to configure recording: " + std::string(error.what()));
    impl_.reset();
    return false;
  }
}

void RecordingRuntime::Stop() {
  if (impl_ == nullptr) {
    return;
  }
  impl_->stopping.store(true);
  if (impl_->worker.joinable()) {
    impl_->worker.join();
  }
  std::string error;
  if (!impl_->service.Stop(&error) && !error.empty()) {
    LOG_ERROR("stop recording failed: " + error);
  }
  impl_->grpc.Shutdown();
  impl_.reset();
}

int RecordingRuntime::Poll() const {
  if (impl_ == nullptr) {
    return 1;
  }
  if (impl_->running.load()) {
    return 0;
  }
  const int result = impl_->result.load();
  return result == 0 ? 1 : result;
}

}  // namespace recording
}  // namespace cockpit
