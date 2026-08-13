#include "agent/audio/audio_playback_client.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include "agent/speech/tts/mock_speech_synthesizer.h"

namespace {

class CancellableSynthesizer final : public cockpit::voice::SpeechSynthesizer {
 public:
  cockpit::voice::SpeechSynthesisResult Synthesize(
      const std::string&, std::chrono::steady_clock::time_point deadline) override {
    std::unique_lock<std::mutex> lock(mutex_);
    entered_ = true;
    changed_.notify_all();
    const auto remaining = deadline - std::chrono::steady_clock::now();
    changed_.wait_until(lock, std::chrono::system_clock::now() + remaining, [this] {
      return cancelled_;
    });
    return {false, {}, "cancellable", cancelled_ ? "cancelled" : "deadline exceeded"};
  }

  void Cancel() override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      cancelled_ = true;
    }
    changed_.notify_all();
  }

  bool WaitUntilEntered() {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_until(lock, std::chrono::system_clock::now() + std::chrono::seconds(1),
                               [this] {
                                 return entered_;
                               });
  }

 private:
  std::mutex mutex_;
  std::condition_variable changed_;
  bool entered_ = false;
  bool cancelled_ = false;
};

class FakeAudioPlaybackTransport final : public cockpit::voice::AudioPlaybackTransport {
 public:
  enum class Behavior {
    kManual,
    kWaitTimeout,
    kNotFound,
  };

  explicit FakeAudioPlaybackTransport(Behavior behavior = Behavior::kManual) : behavior_(behavior) {
  }

  cockpit::voice::AudioPlaybackSubmitResult Submit(std::uint64_t playback_id,
                                                   const cockpit::audio::PcmBuffer&) override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      playback_id_ = playback_id;
      status_ = cockpit::voice::AudioPlaybackWaitStatus::kFailed;
      completed_ = false;
    }
    changed_.notify_all();
    return {cockpit::voice::AudioPlaybackSubmitStatus::kAccepted, {}};
  }

  cockpit::voice::AudioPlaybackWaitResult Wait(std::uint64_t playback_id,
                                               std::chrono::milliseconds timeout) override {
    std::unique_lock<std::mutex> lock(mutex_);
    ++wait_calls_;
    if (behavior_ == Behavior::kWaitTimeout && wait_calls_ == 1U) {
      return {cockpit::voice::AudioPlaybackWaitStatus::kTimeout, "wait timeout"};
    }
    if (behavior_ == Behavior::kNotFound) {
      return {cockpit::voice::AudioPlaybackWaitStatus::kNotFound, "playback not found"};
    }
    changed_.wait_until(lock, std::chrono::system_clock::now() + timeout, [this] {
      return completed_;
    });
    if (playback_id != playback_id_ || !completed_) {
      return {cockpit::voice::AudioPlaybackWaitStatus::kTimeout, "playback result unavailable"};
    }
    return {status_, {}};
  }

  bool Cancel(std::uint64_t playback_id) override {
    bool accepted = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      ++cancel_calls_;
      if (playback_id == playback_id_) {
        status_ = cockpit::voice::AudioPlaybackWaitStatus::kCancelled;
        completed_ = true;
        accepted = true;
      }
    }
    changed_.notify_all();
    return accepted;
  }

  bool WaitForSubmission(std::uint64_t previous_id, std::uint64_t* playback_id) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!changed_.wait_until(lock, std::chrono::system_clock::now() + std::chrono::seconds(1),
                             [this, previous_id] {
                               return playback_id_ != 0U && playback_id_ != previous_id;
                             })) {
      return false;
    }
    *playback_id = playback_id_;
    return true;
  }

  void Complete(std::uint64_t playback_id) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (playback_id_ == playback_id && !completed_) {
        status_ = cockpit::voice::AudioPlaybackWaitStatus::kCompleted;
        completed_ = true;
      }
    }
    changed_.notify_all();
  }

  std::uint64_t cancel_calls() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cancel_calls_;
  }

  std::uint64_t wait_calls() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return wait_calls_;
  }

  cockpit::voice::AudioPlaybackWaitStatus status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
  }

 private:
  const Behavior behavior_;
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  std::uint64_t playback_id_ = 0;
  cockpit::voice::AudioPlaybackWaitStatus status_ =
      cockpit::voice::AudioPlaybackWaitStatus::kFailed;
  bool completed_ = false;
  std::uint64_t cancel_calls_ = 0U;
  std::uint64_t wait_calls_ = 0U;
};

}  // namespace

int main() {
  auto timeout_synthesizer = std::make_unique<CancellableSynthesizer>();
  cockpit::voice::AudioPlaybackClient timeout_client("unix:/tmp/cockpit-unused-audio.sock",
                                                     std::move(timeout_synthesizer),
                                                     std::chrono::milliseconds(10));
  const auto timeout_started = std::chrono::steady_clock::now();
  if (timeout_client.Submit(1U, "timeout synthesis",
                            [](cockpit::voice::VoiceOutputResult) {
                            }) ||
      std::chrono::steady_clock::now() - timeout_started > std::chrono::milliseconds(300) ||
      timeout_client.metrics().failed != 1) {
    std::cerr << "speech synthesis deadline was not enforced\n";
    return 1;
  }

  auto synthesizer = std::make_unique<CancellableSynthesizer>();
  auto* observer = synthesizer.get();
  cockpit::voice::AudioPlaybackClient client("unix:/tmp/cockpit-unused-audio.sock",
                                             std::move(synthesizer), std::chrono::seconds(10));
  std::thread submitter([&client] {
    client.Submit(2U, "cancel synthesis", [](cockpit::voice::VoiceOutputResult) {
    });
  });
  if (!observer->WaitUntilEntered()) {
    std::cerr << "speech synthesizer did not start\n";
    return 1;
  }
  const auto stop_started = std::chrono::steady_clock::now();
  client.Stop();
  submitter.join();
  if (std::chrono::steady_clock::now() - stop_started > std::chrono::milliseconds(300) ||
      client.metrics().failed != 1) {
    std::cerr << "speech synthesis cancellation was not bounded\n";
    return 1;
  }

  auto playback_transport = std::make_unique<FakeAudioPlaybackTransport>();
  auto* audio_control = playback_transport.get();
  cockpit::voice::AudioPlaybackClient playback_client(
      std::move(playback_transport), std::make_unique<cockpit::voice::MockSpeechSynthesizer>());
  std::atomic<std::uint64_t> completions{0};
  std::atomic<cockpit::voice::VoiceOutputStatus> completion_status{
      cockpit::voice::VoiceOutputStatus::kFailed};
  std::thread playback_submitter([&] {
    playback_client.Submit(
        3U, "complete playback",
        [&completions, &completion_status](cockpit::voice::VoiceOutputResult result) {
          completion_status.store(result.status);
          completions.fetch_add(1U);
        });
  });
  std::uint64_t playback_id = 0;
  if (!audio_control->WaitForSubmission(0U, &playback_id) || completions.load() != 0U) {
    std::cerr << "PlayPcm acceptance was treated as playback completion\n";
    return 1;
  }
  audio_control->Complete(playback_id);
  playback_submitter.join();
  if (completions.load() != 1U ||
      completion_status.load() != cockpit::voice::VoiceOutputStatus::kCompleted ||
      playback_client.metrics().played != 1U) {
    std::cerr << "real playback completion was not reported exactly once\n";
    return 1;
  }

  std::thread cancelled_submitter([&] {
    playback_client.Submit(
        4U, "cancel playback",
        [&completions, &completion_status](cockpit::voice::VoiceOutputResult result) {
          completion_status.store(result.status);
          completions.fetch_add(1U);
        });
  });
  const std::uint64_t previous_playback_id = playback_id;
  if (!audio_control->WaitForSubmission(previous_playback_id, &playback_id)) {
    std::cerr << "cancellable playback was not submitted\n";
    return 1;
  }
  playback_client.Interrupt();
  cancelled_submitter.join();
  if (completions.load() != 2U ||
      completion_status.load() != cockpit::voice::VoiceOutputStatus::kCancelled ||
      audio_control->cancel_calls() != 1U) {
    std::cerr << "playback interrupt was not correlated with cancellation completions="
              << completions.load() << " status=" << static_cast<int>(completion_status.load())
              << '\n';
    return 1;
  }
  playback_client.Stop();

  auto timeout_transport = std::make_unique<FakeAudioPlaybackTransport>(
      FakeAudioPlaybackTransport::Behavior::kWaitTimeout);
  auto* timeout_control = timeout_transport.get();
  cockpit::voice::AudioPlaybackClient playback_timeout_client(
      std::move(timeout_transport), std::make_unique<cockpit::voice::MockSpeechSynthesizer>());
  std::uint64_t timeout_completions = 0U;
  cockpit::voice::VoiceOutputStatus timeout_status = cockpit::voice::VoiceOutputStatus::kCompleted;
  if (!playback_timeout_client.Submit(5U, "timeout playback",
                                      [&](cockpit::voice::VoiceOutputResult result) {
                                        ++timeout_completions;
                                        timeout_status = result.status;
                                      }) ||
      timeout_completions != 1U || timeout_status != cockpit::voice::VoiceOutputStatus::kFailed ||
      timeout_control->cancel_calls() != 1U || timeout_control->wait_calls() != 2U ||
      timeout_control->status() != cockpit::voice::AudioPlaybackWaitStatus::kCancelled ||
      playback_timeout_client.metrics().played != 0U ||
      playback_timeout_client.metrics().failed != 1U) {
    std::cerr << "playback timeout did not cancel and confirm the terminal result exactly once\n";
    return 1;
  }
  playback_timeout_client.Interrupt();
  playback_timeout_client.Stop();
  if (timeout_control->cancel_calls() != 1U || timeout_completions != 1U) {
    std::cerr << "timeout, interrupt, and stop cancellation were not idempotent\n";
    return 1;
  }

  auto missing_transport =
      std::make_unique<FakeAudioPlaybackTransport>(FakeAudioPlaybackTransport::Behavior::kNotFound);
  auto* missing_control = missing_transport.get();
  cockpit::voice::AudioPlaybackClient missing_client(
      std::move(missing_transport), std::make_unique<cockpit::voice::MockSpeechSynthesizer>());
  std::uint64_t missing_completions = 0U;
  const auto missing_started = std::chrono::steady_clock::now();
  if (!missing_client.Submit(6U, "driver restarted",
                             [&](cockpit::voice::VoiceOutputResult result) {
                               if (result.status == cockpit::voice::VoiceOutputStatus::kFailed) {
                                 ++missing_completions;
                               }
                             }) ||
      missing_completions != 1U || missing_control->cancel_calls() != 0U ||
      std::chrono::steady_clock::now() - missing_started > std::chrono::milliseconds(300)) {
    std::cerr << "NOT_FOUND playback did not fail once within a bounded interval\n";
    return 1;
  }
  missing_client.Stop();

  auto stopping_transport = std::make_unique<FakeAudioPlaybackTransport>();
  auto* stopping_control = stopping_transport.get();
  cockpit::voice::AudioPlaybackClient stopping_client(
      std::move(stopping_transport), std::make_unique<cockpit::voice::MockSpeechSynthesizer>());
  std::atomic<std::uint64_t> stopping_completions{0U};
  std::atomic<cockpit::voice::VoiceOutputStatus> stopping_status{
      cockpit::voice::VoiceOutputStatus::kFailed};
  std::thread stopping_submitter([&] {
    stopping_client.Submit(7U, "stop while waiting", [&](cockpit::voice::VoiceOutputResult result) {
      stopping_status.store(result.status);
      stopping_completions.fetch_add(1U);
    });
  });
  std::uint64_t stopping_playback_id = 0U;
  if (!stopping_control->WaitForSubmission(0U, &stopping_playback_id)) {
    std::cerr << "stop-during-wait playback was not submitted\n";
    return 1;
  }
  const auto playback_stop_started = std::chrono::steady_clock::now();
  stopping_client.Stop();
  stopping_submitter.join();
  if (std::chrono::steady_clock::now() - playback_stop_started > std::chrono::milliseconds(300) ||
      stopping_control->cancel_calls() != 1U || stopping_completions.load() != 1U ||
      stopping_status.load() != cockpit::voice::VoiceOutputStatus::kCancelled) {
    std::cerr << "Stop during Wait deadlocked or completed incorrectly\n";
    return 1;
  }
  std::cout << "audio playback client tests passed\n";
  return 0;
}
