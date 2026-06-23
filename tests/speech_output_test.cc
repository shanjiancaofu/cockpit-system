#include "services/audio-service/speech_output.h"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>

#include "modules/audio/audio_player.h"
#include "modules/voice/mock_speech_synthesizer.h"

namespace {

class FakeAudioPlayer final : public cockpit::audio::AudioPlayer {
 public:
  bool Play(const std::string& device, const cockpit::audio::PcmBuffer& buffer,
            std::string* error) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (device != "test-output" || buffer.samples.empty() ||
        buffer.format.sample_rate_hz != 16000 || buffer.format.channels != 1) {
      if (error != nullptr) {
        *error = "invalid fake playback request";
      }
      return false;
    }
    ++play_count_;
    played_.notify_all();
    return true;
  }

  bool WaitForPlay() {
    std::unique_lock<std::mutex> lock(mutex_);
    return played_.wait_for(lock, std::chrono::seconds(1), [this] {
      return play_count_ > 0;
    });
  }

 private:
  std::mutex mutex_;
  std::condition_variable played_;
  int play_count_ = 0;
};

}  // namespace

int main() {
  cockpit::voice::MockSpeechSynthesizer synthesizer;
  const auto empty = synthesizer.Synthesize("");
  const auto tone = synthesizer.Synthesize("System ready.");
  if (empty.success || !tone.success || tone.provider != "mock" || tone.audio.samples.empty() ||
      tone.audio.format.sample_rate_hz != 16000 || tone.audio.format.channels != 1) {
    std::cerr << "mock speech synthesis is invalid\n";
    return 1;
  }

  auto player = std::make_unique<FakeAudioPlayer>();
  auto* player_observer = player.get();
  cockpit::audio::SpeechOutput output(
      "test-output", std::make_unique<cockpit::voice::MockSpeechSynthesizer>(), std::move(player));
  std::string error;
  if (!output.Start(&error) || !output.Submit("Open camera completed.") ||
      !player_observer->WaitForPlay()) {
    std::cerr << "asynchronous speech output failed: " << error << '\n';
    return 1;
  }
  output.Stop();
  const auto metrics = output.metrics();
  if (metrics.queued != 1 || metrics.played != 1 || metrics.failed != 0 || metrics.dropped != 0 ||
      output.Submit("after stop")) {
    std::cerr << "speech output metrics or lifecycle are invalid\n";
    return 1;
  }

  cockpit::audio::SpeechOutput invalid("test-output", nullptr, nullptr);
  if (invalid.Start(&error)) {
    std::cerr << "invalid speech output dependencies were accepted\n";
    return 1;
  }
  std::cout << "speech output tests passed\n";
  return 0;
}
