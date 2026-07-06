#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "cockpit/core/config/system_config.h"
#include "cockpit/core/runtime/ModuleManager.h"
#include "cockpit/modules/audio/capture/audio_capture_source.h"
#include "cockpit/modules/audio/capture/audio_capture_stream.h"
#include "cockpit/modules/audio/frames/pcm_format.h"
#include "cockpit/modules/audio/frames/spsc_ring_buffer.h"
#include "cockpit/modules/audio/vad/speech_segmenter.h"
#include "cockpit/modules/audio/vad/voice_activity_detector.h"
#include "cockpit/modules/voice/asr/speech_recognizer.h"
#include "cockpit/modules/voice/assistant/speech_transcript.h"
#include "cockpit/services/audio-service/capture/audio_capture_module.h"

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
  std::uint64_t speech_segments_completed = 0;
  std::uint64_t speech_segments_truncated = 0;
  std::uint64_t speech_segments_dropped = 0;
  std::uint64_t last_segment_duration_ms = 0;
  std::uint64_t asr_segments_processed = 0;
  std::uint64_t transcripts_published = 0;
  std::uint64_t asr_errors = 0;
  bool asr_enabled = false;
  bool vad_enabled = false;
  std::vector<runtime::ModuleStatus> modules;
  std::string last_error;
};

// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
class AudioService {
 public:
  using SourceFactory =
      std::function<std::unique_ptr<AudioCaptureSource>(const std::string&, const PcmFormat&)>;

  explicit AudioService(config::AudioConfig config);
  AudioService(config::AudioConfig config, SourceFactory source_factory);
  AudioService(config::AudioConfig config, config::VadConfig vad_config);
  AudioService(config::AudioConfig config, config::VadConfig vad_config,
               SourceFactory source_factory);
  AudioService(config::AudioConfig config, config::VadConfig vad_config,
               config::SpeechSegmentConfig segment_config);
  AudioService(config::AudioConfig config, config::VadConfig vad_config,
               config::SpeechSegmentConfig segment_config,
               std::unique_ptr<voice::SpeechRecognizer> recognizer);
  AudioService(config::AudioConfig config, config::VadConfig vad_config,
               config::SpeechSegmentConfig segment_config, SourceFactory source_factory);
  AudioService(config::AudioConfig config, config::VadConfig vad_config,
               config::SpeechSegmentConfig segment_config, SourceFactory source_factory,
               std::unique_ptr<voice::SpeechRecognizer> recognizer);
  ~AudioService();

  AudioService(const AudioService&) = delete;
  AudioService& operator=(const AudioService&) = delete;

  bool StartCapture(const std::string& input_device, std::string* error = nullptr);
  void StopCapture();
  std::optional<SpeechSegment> TryPopSpeechSegment();
  bool WaitForTranscript(std::uint64_t after_id, std::chrono::milliseconds timeout,
                         voice::SpeechTranscript* transcript) const;
  AudioServiceStatus status() const;

 private:
  void StopCaptureLocked();
  AudioCaptureState CaptureStateLocked() const;
  void ProcessVoiceActivity();
  void ResetVadMetrics();
  void PublishSpeechSegment(SpeechSegment segment);
  void ProcessSpeechSegments();
  void PublishTranscript(const SpeechSegment& segment,
                         const voice::SpeechRecognitionResult& result);
  void ResetAsrMetrics();

  const config::AudioConfig config_;
  const config::VadConfig vad_config_;
  const config::SpeechSegmentConfig segment_config_;
  const SourceFactory source_factory_;
  mutable std::mutex mutex_;
  runtime::ModuleManager module_manager_;
  AudioCaptureModule* capture_module_{nullptr};
  std::unique_ptr<VoiceActivityDetector> vad_;
  std::unique_ptr<SpeechSegmenter> segmenter_;
  std::unique_ptr<voice::SpeechRecognizer> recognizer_;
  SpscRingBuffer<SpeechSegment, 8> speech_segments_;
  std::string input_device_;
  std::atomic_bool vad_stop_{false};
  std::thread vad_worker_;
  std::atomic_bool asr_stop_{false};
  std::thread asr_worker_;
  std::atomic<VoiceActivityState> voice_activity_state_{VoiceActivityState::kSilence};
  std::atomic<std::int32_t> input_level_millidbfs_{-120000};
  std::atomic<std::uint64_t> vad_frames_processed_{0};
  std::atomic<std::uint64_t> vad_speech_frames_{0};
  std::atomic<std::uint64_t> vad_speech_events_{0};
  std::atomic<std::uint64_t> vad_silence_events_{0};
  std::atomic<std::uint64_t> speech_segments_completed_{0};
  std::atomic<std::uint64_t> speech_segments_truncated_{0};
  std::atomic<std::uint64_t> last_segment_duration_ms_{0};
  std::atomic<std::uint64_t> asr_segments_processed_{0};
  std::atomic<std::uint64_t> transcripts_published_{0};
  std::atomic<std::uint64_t> asr_errors_{0};
  mutable std::mutex transcript_mutex_;
  mutable std::condition_variable transcript_changed_;
  std::deque<voice::SpeechTranscript> transcript_history_;
  std::uint64_t transcript_version_ = 0;
};

}  // namespace audio
}  // namespace cockpit
