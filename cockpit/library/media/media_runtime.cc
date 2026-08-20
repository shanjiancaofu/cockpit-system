#include "cockpit/library/media/media_runtime.h"

#include <exception>
#include <memory>
#include <string>

#include "cockpit/core/config/system_config.h"
#include "cockpit/core/logging/logger.h"
#include "cockpit/library/media/media_grpc_service.h"
#include "cockpit/library/media/media_service.h"
#include "cockpit/modules/media/media_player.h"

namespace cockpit {
namespace media {

MediaRuntime::MediaRuntime() = default;

MediaRuntime::~MediaRuntime() {
  Stop();
}

bool MediaRuntime::Start(const std::string& config_path) {
  if (service_ != nullptr || grpc_ != nullptr) {
    return false;
  }
  try {
    const auto config = config::SystemConfig::LoadFromFile(config_path);
    logging::InitLogger("media", config.paths().log_dir,
                        logging::ParseLevel(config.logging().level), config.logging().mirror_stderr,
                        config.logging().dump_time_secs, config.logging().cut_off_time_mins,
                        config.logging().max_files);
    std::unique_ptr<MediaPlayer> player = config.services().media.provider == "mock"
                                              ? CreateMockMediaPlayer()
                                              : CreateDisabledMediaPlayer();
    service_ = std::make_unique<MediaService>(std::move(player));
    grpc_ = std::make_unique<MediaGrpcService>(*service_);
    if (!grpc_->Start(config.services().media.grpc.listen_address)) {
      Stop();
      return false;
    }
    return true;
  } catch (const std::exception& error) {
    LOG_ERROR("failed to configure media service: " + std::string(error.what()));
    Stop();
    return false;
  }
}

void MediaRuntime::Stop() {
  if (grpc_ != nullptr) {
    grpc_->Shutdown();
  }
  grpc_.reset();
  service_.reset();
}

int MediaRuntime::Poll() const {
  return service_ != nullptr && grpc_ != nullptr ? 0 : 1;
}

}  // namespace media
}  // namespace cockpit
