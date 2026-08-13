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
  cockpit::voice::AudioPlaybackSubmitResult Submit(std::uint64_t playback_id,
                                                   const cockpit::audio::PcmBuffer&) override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      playback_id_ = playback_id;
      status_ = cockpit::voice::VoiceOutputStatus::kFailed;
      completed_ = false;
    }
    changed_.notify_all();
    return {cockpit::voice::AudioPlaybackSubmitStatus::kAccepted, {}};
  }

  cockpit::voice::VoiceOutputResult Wait(std::uint64_t request_id, std::uint64_t playback_id,
                                         std::chrono::milliseconds timeout) override {
    std::unique_lock<std::mutex> lock(mutex_);
    changed_.wait_until(lock, std::chrono::system_clock::now() + timeout, [this] {
      return completed_;
    });
    if (playback_id != playback_id_ || !completed_) {
      return {request_id, cockpit::voice::VoiceOutputStatus::kFailed,
              "playback result unavailable"};
    }
    return {request_id, status_, {}};
  }

  void Cancel(std::uint64_t playback_id) override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (playback_id == playback_id_) {
        status_ = cockpit::voice::VoiceOutputStatus::kCancelled;
        completed_ = true;
      }
    }
    changed_.notify_all();
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
      if (playback_id_ == playback_id) {
        status_ = cockpit::voice::VoiceOutputStatus::kCompleted;
        completed_ = true;
      }
    }
    changed_.notify_all();
  }

 private:
  std::mutex mutex_;
  std::condition_variable changed_;
  std::uint64_t playback_id_ = 0;
  cockpit::voice::VoiceOutputStatus status_ = cockpit::voice::VoiceOutputStatus::kFailed;
  bool completed_ = false;
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
      completion_status.load() != cockpit::voice::VoiceOutputStatus::kCancelled) {
    std::cerr << "playback interrupt was not correlated with cancellation completions="
              << completions.load() << " status=" << static_cast<int>(completion_status.load())
              << '\n';
    return 1;
  }
  playback_client.Stop();
  std::cout << "audio playback client tests passed\n";
  return 0;
}
