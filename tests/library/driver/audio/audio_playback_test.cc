#include "cockpit/library/driver/audio/playback/audio_playback.h"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "agent/speech/tts/mock_speech_synthesizer.h"
#include "cockpit/modules/audio/playback/audio_player.h"

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
    return played_.wait_until(lock, std::chrono::system_clock::now() + std::chrono::seconds(1),
                              [this] {
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
    return entered_changed_.wait_until(
        lock, std::chrono::system_clock::now() + std::chrono::seconds(1), [this] {
          return entered_;
        });
  }

 private:
  std::mutex mutex_;
  std::condition_variable entered_changed_;
  bool entered_ = false;
};

class FailingAudioPlayer final : public cockpit::audio::AudioPlayer {
 public:
  bool Play(const std::string&, const cockpit::audio::PcmBuffer&, const std::atomic_bool&,
            std::string* error) override {
    if (error != nullptr) {
      *error = "injected playback failure";
    }
    return false;
  }
};

}  // namespace

int main() {
  cockpit::voice::MockSpeechSynthesizer synthesizer;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  const auto empty = synthesizer.Synthesize("", deadline);
  auto tone = synthesizer.Synthesize("System ready.", deadline);
  if (empty.success || !tone.success || tone.provider != "mock" || tone.audio.samples.empty() ||
      tone.audio.format.sample_rate_hz != 16000 || tone.audio.format.channels != 1) {
    std::cerr << "mock speech synthesis is invalid\n";
    return 1;
  }

  auto player = std::make_unique<FakeAudioPlayer>();
  auto* player_observer = player.get();
  cockpit::audio::AudioPlayback output("test-output", std::move(player));
  std::string error;
  const auto playback_id =
      output.Start(&error) ? output.Submit(std::move(tone.audio)) : std::nullopt;
  if (!playback_id.has_value() || !player_observer->WaitForPlay()) {
    std::cerr << "asynchronous audio playback failed: " << error << '\n';
    return 1;
  }
  cockpit::audio::AudioPlaybackResult playback_result;
  if (output.WaitForResult(*playback_id, std::chrono::seconds(1), &playback_result) !=
          cockpit::audio::AudioPlaybackWaitStatus::kReady ||
      playback_result.status != cockpit::audio::AudioPlaybackStatus::kCompleted ||
      output.WaitForResult(*playback_id, std::chrono::milliseconds::zero(), &playback_result) !=
          cockpit::audio::AudioPlaybackWaitStatus::kReady) {
    std::cerr << "audio worker did not retain its real completion result\n";
    return 1;
  }
  output.Stop();
  const auto metrics = output.metrics();
  if (metrics.queued != 1 || metrics.played != 1 || metrics.failed != 0 || metrics.dropped != 0 ||
      output.Submit({})) {
    std::cerr << "audio playback metrics or lifecycle are invalid\n";
    return 1;
  }

  cockpit::audio::AudioPlayback invalid("test-output", nullptr);
  if (invalid.Start(&error)) {
    std::cerr << "invalid audio playback dependencies were accepted\n";
    return 1;
  }

  cockpit::audio::AudioPlayback failing("test-output", std::make_unique<FailingAudioPlayer>());
  auto failing_tone = synthesizer.Synthesize("fail playback", deadline);
  const bool failing_started = failing.Start(&error);
  const auto failing_id =
      failing_started ? failing.Submit(std::move(failing_tone.audio)) : std::nullopt;
  if (!failing_id.has_value() ||
      failing.WaitForResult(*failing_id, std::chrono::seconds(1), &playback_result) !=
          cockpit::audio::AudioPlaybackWaitStatus::kReady ||
      playback_result.status != cockpit::audio::AudioPlaybackStatus::kFailed ||
      failing.metrics().failed != 1U) {
    std::cerr << "audio worker failure did not produce a correlated final result\n";
    return 1;
  }
  failing.Stop();

  auto blocking_player = std::make_unique<BlockingAudioPlayer>();
  auto* blocking_observer = blocking_player.get();
  cockpit::audio::AudioPlayback cancellable("test-output", std::move(blocking_player));
  auto cancellable_tone = synthesizer.Synthesize("cancel playback", deadline);
  const bool cancellable_started = cancellable.Start(&error);
  const auto cancellable_id =
      cancellable_started ? cancellable.Submit(std::move(cancellable_tone.audio)) : std::nullopt;
  if (!cancellable_id.has_value() || !blocking_observer->WaitUntilEntered()) {
    std::cerr << "cancellable speech output did not enter playback\n";
    return 1;
  }
  if (cancellable.WaitForResult(*cancellable_id, std::chrono::milliseconds::zero(), nullptr) !=
          cockpit::audio::AudioPlaybackWaitStatus::kTimeout ||
      !cancellable.Cancel(*cancellable_id) || !cancellable.Cancel(*cancellable_id) ||
      cancellable.WaitForResult(*cancellable_id, std::chrono::seconds(1), &playback_result) !=
          cockpit::audio::AudioPlaybackWaitStatus::kReady ||
      playback_result.status != cockpit::audio::AudioPlaybackStatus::kCancelled) {
    std::cerr << "accepted playback did not remain pending until its real cancellation\n";
    return 1;
  }
  const auto stop_started = std::chrono::steady_clock::now();
  cancellable.Stop();
  if (std::chrono::steady_clock::now() - stop_started > std::chrono::milliseconds(300) ||
      cancellable.metrics().played != 0U || cancellable.metrics().dropped != 1U) {
    std::cerr << "speech output cancellation was not bounded\n";
    return 1;
  }
  std::cout << "audio playback tests passed\n";
  return 0;
}
