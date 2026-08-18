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
    deadline_propagated_ = deadline > std::chrono::steady_clock::now();
    changed_.wait(lock, [this] {
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

  bool deadline_propagated() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return deadline_propagated_;
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  bool entered_ = false;
  bool cancelled_ = false;
  bool deadline_propagated_ = false;
};

class FakeAudioPlaybackTransport final : public cockpit::voice::AudioPlaybackTransport {
 public:
  enum class Behavior {
    kManual,
    kAutoComplete,
    kWaitTimeout,
    kTransportError,
    kCancelRetry,
    kUncertain,
    kConcurrentCancel,
    kNotFound,
  };

  explicit FakeAudioPlaybackTransport(Behavior behavior = Behavior::kManual) : behavior_(behavior) {
  }

  cockpit::voice::AudioPlaybackSubmitResult Submit(std::uint64_t playback_id,
                                                   const cockpit::audio::PcmBuffer&) override {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      playback_id_ = playback_id;
      status_ = cockpit::voice::AudioPlaybackWaitStatus::kFailed;
      completed_ = false;
      if (behavior_ == Behavior::kAutoComplete) {
        status_ = cockpit::voice::AudioPlaybackWaitStatus::kCompleted;
        completed_ = true;
      }
      changed_.notify_all();
      if (behavior_ == Behavior::kConcurrentCancel) {
        changed_.wait(lock, [this] {
          return submission_released_;
        });
      }
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
    if ((behavior_ == Behavior::kTransportError || behavior_ == Behavior::kCancelRetry) &&
        wait_calls_ == 1U) {
      return {cockpit::voice::AudioPlaybackWaitStatus::kTransportError,
              "playback transport disconnected"};
    }
    if (behavior_ == Behavior::kUncertain) {
      return {cockpit::voice::AudioPlaybackWaitStatus::kTransportError,
              "playback terminal state unavailable"};
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
      std::unique_lock<std::mutex> lock(mutex_);
      ++cancel_calls_;
      changed_.notify_all();
      if (behavior_ == Behavior::kConcurrentCancel && cancel_calls_ == 1U) {
        changed_.wait(lock, [this] {
          return cancel_released_;
        });
      }
      const bool reject = behavior_ == Behavior::kUncertain ||
                          (behavior_ == Behavior::kCancelRetry && cancel_calls_ == 1U);
      if (!reject && playback_id == playback_id_) {
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

  bool WaitForCancelCalls(std::uint64_t count, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_until(lock, std::chrono::system_clock::now() + timeout, [this, count] {
      return cancel_calls_ >= count;
    });
  }

  void ReleaseSubmission() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      submission_released_ = true;
    }
    changed_.notify_all();
  }

  void ReleaseCancel() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      cancel_released_ = true;
    }
    changed_.notify_all();
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
  bool submission_released_ = false;
  bool cancel_released_ = false;
  std::uint64_t cancel_calls_ = 0U;
  std::uint64_t wait_calls_ = 0U;
};

}  // namespace

int main() {
  auto timeout_synthesizer = std::make_unique<CancellableSynthesizer>();
  auto* timeout_synthesizer_observer = timeout_synthesizer.get();
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
  if (timeout_client.metrics().tts_timeouts != 1U ||
      !timeout_synthesizer_observer->deadline_propagated()) {
    std::cerr << "speech synthesis deadline or timeout metric was not propagated\n";
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

  auto segmented_transport = std::make_unique<FakeAudioPlaybackTransport>(
      FakeAudioPlaybackTransport::Behavior::kAutoComplete);
  auto* segmented_control = segmented_transport.get();
  cockpit::voice::AudioPlaybackClient segmented_client(
      std::move(segmented_transport), std::make_unique<cockpit::voice::MockSpeechSynthesizer>());
  std::atomic<std::uint64_t> segmented_completions{0};
  cockpit::voice::VoiceOutputStatus segmented_status = cockpit::voice::VoiceOutputStatus::kFailed;
  if (!segmented_client.Submit(
          5U, "First sentence. Second sentence!",
          [&segmented_completions, &segmented_status](cockpit::voice::VoiceOutputResult result) {
            segmented_status = result.status;
            segmented_completions.fetch_add(1U);
          }) ||
      segmented_completions.load() != 1U ||
      segmented_status != cockpit::voice::VoiceOutputStatus::kCompleted ||
      segmented_control->wait_calls() != 2U || segmented_client.metrics().played != 2U) {
    std::cerr << "sentence-segmented playback did not complete each segment exactly once\n";
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

  auto transport_error_transport = std::make_unique<FakeAudioPlaybackTransport>(
      FakeAudioPlaybackTransport::Behavior::kTransportError);
  auto* transport_error_control = transport_error_transport.get();
  cockpit::voice::AudioPlaybackClient transport_error_client(
      std::move(transport_error_transport),
      std::make_unique<cockpit::voice::MockSpeechSynthesizer>());
  std::uint64_t transport_error_completions = 0U;
  cockpit::voice::VoiceOutputResult transport_error_result;
  if (!transport_error_client.Submit(8U, "transport error playback",
                                     [&](cockpit::voice::VoiceOutputResult result) {
                                       ++transport_error_completions;
                                       transport_error_result = std::move(result);
                                     }) ||
      transport_error_completions != 1U ||
      transport_error_result.status != cockpit::voice::VoiceOutputStatus::kFailed ||
      transport_error_control->cancel_calls() != 1U ||
      transport_error_control->wait_calls() != 2U) {
    std::cerr << "accepted playback transport error was not cancelled and failed once\n";
    return 1;
  }
  transport_error_client.Stop();

  auto retry_transport = std::make_unique<FakeAudioPlaybackTransport>(
      FakeAudioPlaybackTransport::Behavior::kCancelRetry);
  auto* retry_control = retry_transport.get();
  cockpit::voice::AudioPlaybackClient retry_client(
      std::move(retry_transport), std::make_unique<cockpit::voice::MockSpeechSynthesizer>());
  cockpit::voice::VoiceOutputStatus retry_status = cockpit::voice::VoiceOutputStatus::kCancelled;
  if (!retry_client.Submit(9U, "retry playback cancellation",
                           [&](cockpit::voice::VoiceOutputResult result) {
                             retry_status = result.status;
                           }) ||
      retry_status != cockpit::voice::VoiceOutputStatus::kFailed ||
      retry_control->cancel_calls() != 2U || retry_control->wait_calls() != 2U) {
    std::cerr << "failed Cancel RPC was not retried within the fixed bound\n";
    return 1;
  }
  retry_client.Stop();

  auto uncertain_transport = std::make_unique<FakeAudioPlaybackTransport>(
      FakeAudioPlaybackTransport::Behavior::kUncertain);
  auto* uncertain_control = uncertain_transport.get();
  cockpit::voice::AudioPlaybackClient uncertain_client(
      std::move(uncertain_transport), std::make_unique<cockpit::voice::MockSpeechSynthesizer>());
  cockpit::voice::VoiceOutputResult uncertain_result;
  if (!uncertain_client.Submit(10U, "uncertain playback",
                               [&](cockpit::voice::VoiceOutputResult result) {
                                 uncertain_result = std::move(result);
                               }) ||
      uncertain_result.status != cockpit::voice::VoiceOutputStatus::kFailed ||
      uncertain_result.error.find("remained uncertain") == std::string::npos ||
      uncertain_control->cancel_calls() != 2U || uncertain_control->wait_calls() != 2U) {
    std::cerr << "unconfirmed playback terminal state was not a bounded uncertainty failure\n";
    return 1;
  }
  uncertain_client.Stop();

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

  auto stopping_transport = std::make_unique<FakeAudioPlaybackTransport>(
      FakeAudioPlaybackTransport::Behavior::kConcurrentCancel);
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
  std::atomic_bool stop_returned{false};
  std::thread stopper([&] {
    stopping_client.Stop();
    stop_returned.store(true);
  });
  if (!stopping_control->WaitForCancelCalls(1U, std::chrono::seconds(1))) {
    std::cerr << "Stop did not start playback cancellation\n";
    stopping_control->ReleaseSubmission();
    stopping_control->ReleaseCancel();
    stopper.join();
    stopping_submitter.join();
    return 1;
  }
  stopping_control->ReleaseSubmission();
  const bool duplicate_cancel =
      stopping_control->WaitForCancelCalls(2U, std::chrono::milliseconds(100));
  const auto playback_stop_started = std::chrono::steady_clock::now();
  stopping_control->ReleaseCancel();
  stopper.join();
  stopping_submitter.join();
  if (duplicate_cancel || !stop_returned.load() ||
      std::chrono::steady_clock::now() - playback_stop_started > std::chrono::milliseconds(300) ||
      stopping_control->cancel_calls() != 1U || stopping_completions.load() != 1U ||
      stopping_status.load() != cockpit::voice::VoiceOutputStatus::kCancelled) {
    std::cerr << "Stop during Wait deadlocked or completed incorrectly\n";
    return 1;
  }
  std::cout << "audio playback client tests passed\n";
  return 0;
}
