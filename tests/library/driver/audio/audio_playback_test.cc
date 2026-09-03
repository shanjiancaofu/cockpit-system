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

  cockpit::audio::PcmBuffer cancellation_stress_buffer;
  cancellation_stress_buffer.format.sample_rate_hz = 16000;
  cancellation_stress_buffer.format.channels = 1;
  cancellation_stress_buffer.format.frame_ms = 20;
  cancellation_stress_buffer.samples.assign(320, 0);
  for (std::uint64_t iteration = 1U; iteration <= 128U; ++iteration) {
    auto stress_player = std::make_unique<BlockingAudioPlayer>();
    auto* stress_observer = stress_player.get();
    cockpit::audio::AudioPlayback stress_output("test-output", std::move(stress_player));
    if (!stress_output.Start(&error)) {
      std::cerr << "playback cancellation stress failed to start\n";
      return 1;
    }
    const auto stress_id = stress_output.Submit(cancellation_stress_buffer, 1000U + iteration);
    if (!stress_id.has_value() || !stress_observer->WaitUntilEntered()) {
      std::cerr << "playback cancellation stress did not enter the player\n";
      return 1;
    }
    cockpit::audio::AudioPlaybackResult stress_result;
    std::thread waiter([&] {
      if (stress_output.WaitForResult(*stress_id, std::chrono::seconds(1), &stress_result) !=
          cockpit::audio::AudioPlaybackWaitStatus::kReady) {
        stress_result.status = cockpit::audio::AudioPlaybackStatus::kFailed;
      }
    });
    if (!stress_output.Cancel(*stress_id)) {
      waiter.join();
      std::cerr << "playback cancellation stress rejected cancellation\n";
      return 1;
    }
    waiter.join();
    stress_output.Stop();
    if (stress_result.status != cockpit::audio::AudioPlaybackStatus::kCancelled ||
        stress_output.metrics().dropped != 1U) {
      std::cerr << "playback cancellation stress lost its terminal result\n";
      return 1;
    }
  }

  auto queue_player = std::make_unique<BlockingAudioPlayer>();
  auto* queue_player_observer = queue_player.get();
  cockpit::audio::AudioPlayback bounded_queue("test-output", std::move(queue_player));
  cockpit::audio::PcmBuffer queue_buffer;
  queue_buffer.format.sample_rate_hz = 16000;
  queue_buffer.format.channels = 1;
  queue_buffer.samples.assign(160, 0);
  if (!bounded_queue.Start(&error)) {
    std::cerr << "bounded playback queue failed to start: " << error << '\n';
    return 1;
  }
  const auto active_id = bounded_queue.Submit(queue_buffer);
  if (!active_id.has_value() || !queue_player_observer->WaitUntilEntered()) {
    std::cerr << "bounded playback queue did not start its active request\n";
    return 1;
  }
  std::size_t accepted_queued = 0U;
  for (; accepted_queued < 8U; ++accepted_queued) {
    if (!bounded_queue.Submit(queue_buffer).has_value()) {
      std::cerr << "bounded playback queue rejected an item before reaching capacity\n";
      return 1;
    }
  }
  if (bounded_queue.Submit(queue_buffer).has_value()) {
    std::cerr << "bounded playback queue accepted an item beyond capacity\n";
    return 1;
  }
  bounded_queue.Stop();
  const auto bounded_metrics = bounded_queue.metrics();
  if (bounded_metrics.queued != 9U || bounded_metrics.dropped != 10U) {
    std::cerr << "bounded playback queue did not drop active and queued items on stop: queued="
              << bounded_metrics.queued << " dropped=" << bounded_metrics.dropped << '\n';
    return 1;
  }
  std::cout << "audio playback tests passed\n";
  return 0;
}
