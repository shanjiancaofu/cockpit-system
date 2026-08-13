#include "cockpit/library/driver/audio/audio_runtime.h"

#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include "cockpit/core/config/system_config.h"
#include "cockpit/core/logging/logger.h"
#include "cockpit/library/driver/audio/capture/audio_capture_controller.h"
#include "cockpit/library/driver/audio/grpc/audio_grpc_server.h"
#include "cockpit/library/driver/audio/playback/audio_playback.h"
#include "cockpit/library/driver/audio/transport/audio_stream_publisher.h"
#include "cockpit/modules/audio/playback/alsa_audio_player.h"

namespace cockpit {
namespace audio {

class AudioRuntime::Impl {
 public:
  std::unique_ptr<AudioCaptureController> capture;
  std::unique_ptr<AudioStreamPublisher> stream_publisher;
  std::unique_ptr<AudioPlayback> playback;
  std::unique_ptr<AudioGrpcServer> grpc;
};

AudioRuntime::AudioRuntime() = default;

AudioRuntime::~AudioRuntime() {
  Stop();
}

bool AudioRuntime::Start(const std::string& config_path,
                         const std::string& output_device_override) {
  if (impl_ != nullptr) {
    return false;
  }
  try {
    const auto config = config::SystemConfig::LoadFromFile(config_path);
    logging::InitLogger("audio_driver", config.paths().log_dir,
                        logging::ParseLevel(config.logging().level), config.logging().mirror_stderr,
                        config.logging().dump_time_secs, config.logging().cut_off_time_mins,
                        config.logging().max_files);
    impl_ = std::make_unique<Impl>();
    impl_->stream_publisher = std::make_unique<AudioStreamPublisher>();
    const std::filesystem::path run_dir = std::filesystem::absolute(config.paths().run_dir);
    std::error_code directory_error;
    std::filesystem::create_directories(run_dir, directory_error);
    if (directory_error) {
      LOG_ERROR("failed to create audio runtime directory: " + directory_error.message());
      impl_.reset();
      return false;
    }
    const std::string stream_socket = (run_dir / "audio-capture.sock").string();
    std::string error;
    if (!impl_->stream_publisher->Start(stream_socket, &error)) {
      LOG_ERROR("failed to start audio stream publisher: " + error);
      impl_.reset();
      return false;
    }
    impl_->capture =
        std::make_unique<AudioCaptureController>(config.hardware().audio, *impl_->stream_publisher);
    const std::string output_device = output_device_override.empty()
                                          ? config.hardware().audio.output_device
                                          : output_device_override;
    impl_->playback =
        std::make_unique<AudioPlayback>(output_device, std::make_unique<AlsaAudioPlayer>());
    if (!impl_->playback->Start(&error)) {
      LOG_ERROR("failed to start audio playback: " + error);
      impl_.reset();
      return false;
    }
    impl_->grpc = std::make_unique<AudioGrpcServer>(*impl_->capture, *impl_->playback);
    if (!impl_->grpc->Start(config.services().audio.grpc.listen_address)) {
      impl_.reset();
      return false;
    }
    if (config.services().audio.auto_start && !impl_->capture->Start("", &error)) {
      LOG_ERROR("failed to auto-start audio capture: " + error);
      impl_.reset();
      return false;
    }
    return true;
  } catch (const std::exception& error) {
    LOG_ERROR("failed to configure audio driver: " + std::string(error.what()));
    impl_.reset();
    return false;
  }
}

void AudioRuntime::Stop() {
  if (impl_ == nullptr) {
    return;
  }
  impl_->capture->Stop();
  impl_->stream_publisher->Stop();
  // Finalize playback results before waiting for blocking WaitPlayback RPCs to drain.
  impl_->playback->Stop();
  impl_->grpc->Shutdown();
  impl_.reset();
}

int AudioRuntime::Poll() const {
  if (impl_ == nullptr) {
    return 1;
  }
  return impl_->capture->faulted() ? 2 : 0;
}

}  // namespace audio
}  // namespace cockpit
