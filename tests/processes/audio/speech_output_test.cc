#include "cockpit/library/driver/audio/playback/speech_output.h"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "cockpit/modules/audio/playback/audio_player.h"
#include "cockpit/modules/voice/tts/mock_speech_synthesizer.h"

namespace {

class FakeAudioPlayer final : public cockpit::audio::AudioPlayer {
 public:
  bool Play(const std::string& device, const cockpit::audio::PcmBuffer& buffer,
            const std::atomic_bool&, std::string* error) override {
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

class BlockingAudioPlayer final : public cockpit::audio::AudioPlayer {
 public:
  bool Play(const std::string&, const cockpit::audio::PcmBuffer&,
            const std::atomic_bool& stop_requested, std::string*) override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      entered_ = true;
    }
    entered_changed_.notify_all();
    while (!stop_requested.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
  }

  bool WaitUntilEntered() {
    std::unique_lock<std::mutex> lock(mutex_);
    return entered_changed_.wait_for(lock, std::chrono::seconds(1), [this] {
      return entered_;
    });
  }

 private:
  std::mutex mutex_;
  std::condition_variable entered_changed_;
  bool entered_ = false;
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

  auto blocking_player = std::make_unique<BlockingAudioPlayer>();
  auto* blocking_observer = blocking_player.get();
  cockpit::audio::SpeechOutput cancellable(
      "test-output", std::make_unique<cockpit::voice::MockSpeechSynthesizer>(),
      std::move(blocking_player));
  if (!cancellable.Start(&error) || !cancellable.Submit("cancel playback") ||
      !blocking_observer->WaitUntilEntered()) {
    std::cerr << "cancellable speech output did not enter playback\n";
    return 1;
  }
  const auto stop_started = std::chrono::steady_clock::now();
  cancellable.Stop();
  if (std::chrono::steady_clock::now() - stop_started > std::chrono::milliseconds(300) ||
      cancellable.metrics().dropped != 1) {
    std::cerr << "speech output cancellation was not bounded\n";
    return 1;
  }
  std::cout << "speech output tests passed\n";
  return 0;
}
