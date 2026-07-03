#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "modules/audio/playback/audio_player.h"
#include "modules/voice/responses/voice_response_sink.h"
#include "modules/voice/tts/speech_synthesizer.h"

namespace cockpit {
namespace audio {

class SpeechOutput final : public voice::VoiceResponseSink {
 public:
  SpeechOutput(std::string device, std::unique_ptr<voice::SpeechSynthesizer> synthesizer,
               std::unique_ptr<AudioPlayer> player);
  ~SpeechOutput() override;

  SpeechOutput(const SpeechOutput&) = delete;
  SpeechOutput& operator=(const SpeechOutput&) = delete;

  bool Start(std::string* error = nullptr);
  void Stop() override;
  bool Submit(std::string text) override;
  voice::VoiceOutputMetrics metrics() const override;

 private:
  void Run();

  static constexpr std::size_t kQueueCapacity = 8U;
  const std::string device_;
  const std::unique_ptr<voice::SpeechSynthesizer> synthesizer_;
  const std::unique_ptr<AudioPlayer> player_;
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  std::deque<std::string> queue_;
  bool running_ = false;
  bool stop_requested_ = false;
  std::thread worker_;
  std::atomic<std::uint64_t> queued_{0};
  std::atomic<std::uint64_t> played_{0};
  std::atomic<std::uint64_t> failed_{0};
  std::atomic<std::uint64_t> dropped_{0};
};

}  // namespace audio
}  // namespace cockpit
