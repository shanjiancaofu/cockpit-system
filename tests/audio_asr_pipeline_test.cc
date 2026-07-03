#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "modules/voice/asr/mock_speech_recognizer.h"
#include "services/audio-service/audio_service.h"

namespace {

class SpeechCaptureSource final : public cockpit::audio::AudioCaptureSource {
 public:
  bool Open(std::string*) override {
    return true;
  }

  cockpit::audio::CaptureResult Read(std::int16_t* samples, std::size_t frame_capacity, int,
                                     const std::atomic_bool& stop_requested) override {
    if (stop_requested.load()) {
      return {cockpit::audio::CaptureStatus::kStopped, 0, 0, {}};
    }
    for (std::size_t index = 0; index < frame_capacity; ++index) {
      samples[index] = 10000;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return {cockpit::audio::CaptureStatus::kOk, frame_capacity, 0, {}};
  }

  void Close() override {
  }
};

}  // namespace

int main() {
  cockpit::config::AudioConfig audio_config;
  cockpit::config::VadConfig vad_config;
  vad_config.speech_threshold_dbfs = -40.0;
  vad_config.speech_start_frames = 1;
  cockpit::config::SpeechSegmentConfig segment_config;
  segment_config.pre_roll_ms = 0;
  segment_config.max_segment_ms = 100;

  cockpit::audio::AudioService service(
      audio_config, vad_config, segment_config,
      [](const std::string&, const cockpit::audio::PcmFormat&) {
        return std::make_unique<SpeechCaptureSource>();
      },
      std::make_unique<cockpit::voice::MockSpeechRecognizer>());
  std::string error;
  if (!service.StartCapture("fake", &error)) {
    std::cerr << "failed to start ASR pipeline: " << error << '\n';
    return 1;
  }

  cockpit::voice::SpeechTranscript transcript;
  if (!service.WaitForTranscript(0, std::chrono::seconds(1), &transcript)) {
    std::cerr << "ASR pipeline did not publish a transcript\n";
    return 1;
  }
  cockpit::voice::SpeechTranscript next_transcript;
  if (!service.WaitForTranscript(transcript.id, std::chrono::seconds(1), &next_transcript) ||
      next_transcript.id <= transcript.id) {
    std::cerr << "ASR transcript history is not ordered\n";
    return 1;
  }
  const auto status = service.status();
  if (transcript.text != "mock transcript duration_ms=100" || transcript.provider != "mock" ||
      transcript.duration_ms != 100 || status.asr_segments_processed == 0 ||
      status.transcripts_published == 0 || status.asr_errors != 0 || !status.asr_enabled ||
      service.TryPopSpeechSegment().has_value()) {
    std::cerr << "ASR pipeline state is invalid\n";
    return 1;
  }
  service.StopCapture();
  std::cout << "audio ASR pipeline tests passed\n";
  return 0;
}
