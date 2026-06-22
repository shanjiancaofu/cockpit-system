#pragma once

#include "core/config/system_config.h"
#include "modules/audio/audio_capture_source.h"
#include "modules/audio/audio_capture_stream.h"
#include "modules/audio/pcm_format.h"
#include "modules/audio/voice_activity_detector.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace cockpit {
namespace audio {

struct AudioServiceStatus {
  AudioCaptureState capture_state = AudioCaptureState::kStopped;
  std::string input_device;
  std::uint32_t sample_rate_hz = 0;
  std::uint32_t channels = 0;
  std::uint32_t frame_ms = 0;
  AudioCaptureMetrics metrics;
  VoiceActivityState voice_activity_state = VoiceActivityState::kSilence;
  double input_level_dbfs = -120.0;
  std::uint64_t vad_frames_processed = 0;
  std::uint64_t vad_speech_frames = 0;
  std::uint64_t vad_speech_events = 0;
  std::uint64_t vad_silence_events = 0;
  bool vad_enabled = false;
  std::string last_error;
};

class AudioService {
 public:
  using SourceFactory = std::function<std::unique_ptr<AudioCaptureSource>(
      const std::string&, const PcmFormat&)>;

  explicit AudioService(config::AudioConfig config);
  AudioService(config::AudioConfig config, SourceFactory source_factory);
  AudioService(config::AudioConfig config, config::VadConfig vad_config);
  AudioService(config::AudioConfig config, config::VadConfig vad_config,
               SourceFactory source_factory);
  ~AudioService();

  AudioService(const AudioService&) = delete;
  AudioService& operator=(const AudioService&) = delete;

  bool StartCapture(const std::string& input_device, std::string* error = nullptr);
  void StopCapture();
  AudioServiceStatus status() const;

 private:
  void StopCaptureLocked();
  void ProcessVoiceActivity();
  void ResetVadMetrics();

  const config::AudioConfig config_;
  const config::VadConfig vad_config_;
  const SourceFactory source_factory_;
  mutable std::mutex mutex_;
  std::unique_ptr<AudioCaptureStream> capture_stream_;
  std::unique_ptr<VoiceActivityDetector> vad_;
  std::string input_device_;
  std::atomic_bool vad_stop_{false};
  std::thread vad_worker_;
  std::atomic<VoiceActivityState> voice_activity_state_{VoiceActivityState::kSilence};
  std::atomic<std::int32_t> input_level_millidbfs_{-120000};
  std::atomic<std::uint64_t> vad_frames_processed_{0};
  std::atomic<std::uint64_t> vad_speech_frames_{0};
  std::atomic<std::uint64_t> vad_speech_events_{0};
  std::atomic<std::uint64_t> vad_silence_events_{0};
};

}  // namespace audio
}  // namespace cockpit
