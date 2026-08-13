#include "agent/audio/audio_playback_client.h"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

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

}  // namespace

int main() {
  auto timeout_synthesizer = std::make_unique<CancellableSynthesizer>();
  cockpit::voice::AudioPlaybackClient timeout_client("unix:/tmp/cockpit-unused-audio.sock",
                                                     std::move(timeout_synthesizer),
                                                     std::chrono::milliseconds(10));
  const auto timeout_started = std::chrono::steady_clock::now();
  if (timeout_client.Submit("timeout synthesis") ||
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
    client.Submit("cancel synthesis");
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
  std::cout << "audio playback client tests passed\n";
  return 0;
}
