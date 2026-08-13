#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "cockpit/core/base/macros.h"
#include "cockpit/core/config/system_config.h"
#include "cockpit/core/runtime/module_manager.h"
#include "cockpit/library/driver/audio/capture/audio_capture_module.h"
#include "cockpit/library/driver/audio/transport/audio_stream_publisher.h"
#include "cockpit/modules/audio/capture/audio_capture_source.h"
#include "cockpit/modules/audio/frames/pcm_format.h"

namespace cockpit {
namespace audio {

struct AudioCaptureControllerStatus {
  AudioCaptureState capture_state = AudioCaptureState::kStopped;
  std::string input_device;
  std::uint32_t sample_rate_hz = 0;
  std::uint32_t channels = 0;
  std::uint32_t frame_ms = 0;
  AudioCaptureMetrics capture_metrics;
  AudioStreamPublisherMetrics stream_metrics;
  double input_level_dbfs = -120.0;
  std::vector<runtime::ModuleStatus> modules;
  std::string last_error;
};

class AudioCaptureController {
 public:
  using SourceFactory =
      std::function<std::unique_ptr<AudioCaptureSource>(const std::string&, const PcmFormat&)>;

  AudioCaptureController(config::AudioConfig config, AudioStreamPublisher& stream_publisher);
  AudioCaptureController(config::AudioConfig config, SourceFactory source_factory,
                         AudioStreamPublisher& stream_publisher);
  ~AudioCaptureController();

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(AudioCaptureController);

  bool Start(const std::string& input_device, std::string* error = nullptr);
  void Stop();
  AudioCaptureControllerStatus status() const;
  bool faulted() const;

 private:
  void StopLocked();
  AudioCaptureState CaptureStateLocked() const;
  void ProcessFrames();

  const config::AudioConfig config_;
  const SourceFactory source_factory_;
  AudioStreamPublisher& stream_publisher_;
  mutable std::mutex mutex_;
  runtime::ModuleManager module_manager_;
  AudioCaptureModule* capture_module_{nullptr};
  std::string input_device_;
  std::atomic_bool processing_stop_{false};
  std::thread processing_worker_;
  std::atomic<std::int32_t> input_level_millidbfs_{-120000};
};

}  // namespace audio
}  // namespace cockpit
