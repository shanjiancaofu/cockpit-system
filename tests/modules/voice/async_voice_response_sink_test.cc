#include "cockpit/modules/voice/responses/async_voice_response_sink.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace {

struct BlockingState {
  std::mutex mutex;
  std::condition_variable changed;
  bool entered = false;
  bool released = false;
  std::atomic<std::uint64_t> submitted{0};
};

class BlockingSink final : public cockpit::voice::VoiceResponseSink {
 public:
  explicit BlockingSink(std::shared_ptr<BlockingState> state) : state_(std::move(state)) {
  }

  bool Submit(std::string) override {
    {
      std::unique_lock<std::mutex> lock(state_->mutex);
      state_->entered = true;
      state_->changed.notify_all();
      state_->changed.wait(lock, [this] {
        return state_->released;
      });
    }
    state_->submitted.fetch_add(1U);
    return true;
  }

  cockpit::voice::VoiceOutputMetrics metrics() const override {
    return {};
  }

  void Stop() override {
    {
      std::lock_guard<std::mutex> lock(state_->mutex);
      state_->released = true;
    }
    state_->changed.notify_all();
  }

 private:
  const std::shared_ptr<BlockingState> state_;
};

}  // namespace

int main() {
  auto state = std::make_shared<BlockingState>();
  cockpit::voice::AsyncVoiceResponseSink sink(std::make_unique<BlockingSink>(state), 1U);

  const auto start = std::chrono::steady_clock::now();
  if (!sink.Submit("first")) {
    std::cerr << "async sink rejected the first response\n";
    return 1;
  }
  const auto submit_time = std::chrono::steady_clock::now() - start;
  if (submit_time > std::chrono::milliseconds(50)) {
    std::cerr << "async sink blocked on the downstream sink\n";
    return 1;
  }

  {
    std::unique_lock<std::mutex> lock(state->mutex);
    if (!state->changed.wait_for(lock, std::chrono::milliseconds(500), [state] {
          return state->entered;
        })) {
      std::cerr << "async sink worker did not call the downstream sink\n";
      return 1;
    }
  }

  if (!sink.Submit("second") || sink.Submit("overflow")) {
    std::cerr << "async sink capacity handling failed\n";
    return 1;
  }
  const auto metrics = sink.metrics();
  if (metrics.queued != 2 || metrics.dropped != 1) {
    std::cerr << "async sink metrics are invalid\n";
    return 1;
  }

  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->released = true;
  }
  state->changed.notify_all();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
  while (state->submitted.load() < 2 && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (state->submitted.load() != 2) {
    std::cerr << "async sink did not drain accepted responses\n";
    return 1;
  }

  auto cancel_state = std::make_shared<BlockingState>();
  const auto cancel_start = std::chrono::steady_clock::now();
  {
    cockpit::voice::AsyncVoiceResponseSink cancellable(
        std::make_unique<BlockingSink>(cancel_state));
    if (!cancellable.Submit("blocked")) {
      std::cerr << "cancellable sink rejected a response\n";
      return 1;
    }
    std::unique_lock<std::mutex> lock(cancel_state->mutex);
    if (!cancel_state->changed.wait_for(lock, std::chrono::milliseconds(500), [cancel_state] {
          return cancel_state->entered;
        })) {
      std::cerr << "cancellable sink did not enter the downstream call\n";
      return 1;
    }
  }
  if (std::chrono::steady_clock::now() - cancel_start > std::chrono::milliseconds(600)) {
    std::cerr << "async sink cancellation took too long\n";
    return 1;
  }

  std::cout << "async voice response sink tests passed\n";
  return 0;
}
