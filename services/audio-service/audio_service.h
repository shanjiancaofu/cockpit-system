#pragma once

#include "core/config/system_config.h"
#include "modules/audio/audio_capture_source.h"
#include "modules/audio/audio_capture_stream.h"
#include "modules/audio/pcm_format.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace cockpit {
namespace audio {

struct AudioServiceStatus {
  AudioCaptureState capture_state = AudioCaptureState::kStopped;
  std::string input_device;
  std::uint32_t sample_rate_hz = 0;
  std::uint32_t channels = 0;
  std::uint32_t frame_ms = 0;
  AudioCaptureMetrics metrics;
  std::string last_error;
};

class AudioService {
 public:
  using SourceFactory = std::function<std::unique_ptr<AudioCaptureSource>(
      const std::string&, const PcmFormat&)>;

  explicit AudioService(config::AudioConfig config);
  AudioService(config::AudioConfig config, SourceFactory source_factory);
  ~AudioService();

  AudioService(const AudioService&) = delete;
  AudioService& operator=(const AudioService&) = delete;

  bool StartCapture(const std::string& input_device, std::string* error = nullptr);
  void StopCapture();
  std::optional<AudioFrame> TryPopFrame();
  AudioServiceStatus status() const;

 private:
  void StopCaptureLocked();

  const config::AudioConfig config_;
  const SourceFactory source_factory_;
  mutable std::mutex mutex_;
  std::unique_ptr<AudioCaptureStream> capture_stream_;
  std::string input_device_;
};

}  // namespace audio
}  // namespace cockpit
