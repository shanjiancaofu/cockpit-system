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
  std::atomic<std::uint64_t> interrupted{0};
};

class BlockingSink final : public cockpit::voice::VoiceResponseSink {
 public:
  explicit BlockingSink(std::shared_ptr<BlockingState> state) : state_(std::move(state)) {
  }

  bool Submit(std::uint64_t request_id, std::string,
              cockpit::voice::VoiceOutputCompletion completion) override {
    {
      std::unique_lock<std::mutex> lock(state_->mutex);
      state_->entered = true;
      state_->changed.notify_all();
      state_->changed.wait(lock, [this] {
        return state_->released;
      });
    }
    state_->submitted.fetch_add(1U);
    completion({request_id, cockpit::voice::VoiceOutputStatus::kCompleted, {}});
    return true;
  }

  cockpit::voice::VoiceOutputMetrics metrics() const override {
    return {};
  }

  void Stop() override {
    Interrupt();
  }

  void Interrupt() override {
    {
      std::lock_guard<std::mutex> lock(state_->mutex);
      state_->released = true;
    }
    state_->interrupted.fetch_add(1U);
    state_->changed.notify_all();
  }

 private:
  const std::shared_ptr<BlockingState> state_;
};

struct DispatchGateState {
  std::mutex mutex;
  std::condition_variable changed;
  bool before_submit = false;
  bool released = false;
  std::uint64_t submit_calls = 0U;
};

class DispatchGateSink final : public cockpit::voice::VoiceResponseSink {
 public:
  explicit DispatchGateSink(std::shared_ptr<DispatchGateState> state) : state_(std::move(state)) {
  }

  bool Submit(std::uint64_t request_id, std::string,
              cockpit::voice::VoiceOutputCompletion completion) override {
    {
      std::lock_guard<std::mutex> lock(state_->mutex);
      ++state_->submit_calls;
    }
    completion({request_id, cockpit::voice::VoiceOutputStatus::kCompleted, {}});
    return true;
  }

  bool SubmitCancellable(
      std::uint64_t request_id, std::string text,
      const std::shared_ptr<const cockpit::voice::VoiceOutputCancellation>& cancellation,
      cockpit::voice::VoiceOutputCompletion completion) override {
    {
      std::unique_lock<std::mutex> lock(state_->mutex);
      state_->before_submit = true;
      state_->changed.notify_all();
      state_->changed.wait(lock, [this] {
        return state_->released;
      });
    }
    if (cancellation != nullptr && cancellation->IsCancellationRequested()) {
      return false;
    }
    return Submit(request_id, std::move(text), std::move(completion));
  }

  cockpit::voice::VoiceOutputMetrics metrics() const override {
    return {};
  }

  void Interrupt() override {
    {
      std::lock_guard<std::mutex> lock(state_->mutex);
      state_->released = true;
    }
    state_->changed.notify_all();
  }

  void Stop() override {
    Interrupt();
  }

 private:
  const std::shared_ptr<DispatchGateState> state_;
};

class ImmediateFailureSink final : public cockpit::voice::VoiceResponseSink {
 public:
  bool Submit(std::uint64_t request_id, std::string,
              cockpit::voice::VoiceOutputCompletion completion) override {
    completion({request_id, cockpit::voice::VoiceOutputStatus::kFailed, "injected failure"});
    return true;
  }

  cockpit::voice::VoiceOutputMetrics metrics() const override {
    cockpit::voice::VoiceOutputMetrics metrics;
    metrics.played = 17U;
    metrics.failed = 19U;
    metrics.dropped = 23U;
    metrics.available = true;
    return metrics;
  }
};

}  // namespace

int main() {
  auto state = std::make_shared<BlockingState>();
  cockpit::voice::AsyncVoiceResponseSink sink(std::make_unique<BlockingSink>(state), 1U);
  std::atomic<std::uint64_t> completions{0};
  std::atomic<std::uint64_t> cancellations{0};
  const auto on_complete = [&completions,
                            &cancellations](const cockpit::voice::VoiceOutputResult& result) {
    completions.fetch_add(1U);
    if (result.status == cockpit::voice::VoiceOutputStatus::kCancelled) {
      cancellations.fetch_add(1U);
    }
  };

  const auto start = std::chrono::steady_clock::now();
  if (!sink.Submit(1U, "first", on_complete)) {
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
    if (!state->changed.wait_until(
            lock, std::chrono::system_clock::now() + std::chrono::milliseconds(500), [state] {
              return state->entered;
            })) {
      std::cerr << "async sink worker did not call the downstream sink\n";
      return 1;
    }
  }

  if (!sink.Submit(2U, "second", on_complete) || sink.Submit(3U, "overflow", on_complete)) {
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
  if (completions.load() != 2U || cancellations.load() != 0U) {
    std::cerr << "accepted output did not complete exactly once\n";
    return 1;
  }

  auto cancel_state = std::make_shared<BlockingState>();
  const auto cancel_start = std::chrono::steady_clock::now();
  {
    cockpit::voice::AsyncVoiceResponseSink cancellable(
        std::make_unique<BlockingSink>(cancel_state));
    std::atomic<std::uint64_t> cancel_completions{0};
    if (!cancellable.Submit(4U, "blocked",
                            [&cancel_completions](const cockpit::voice::VoiceOutputResult& result) {
                              if (result.status == cockpit::voice::VoiceOutputStatus::kCancelled) {
                                cancel_completions.fetch_add(1U);
                              }
                            })) {
      std::cerr << "cancellable sink rejected a response\n";
      return 1;
    }
    std::unique_lock<std::mutex> lock(cancel_state->mutex);
    if (!cancel_state->changed.wait_until(
            lock, std::chrono::system_clock::now() + std::chrono::milliseconds(500),
            [cancel_state] {
              return cancel_state->entered;
            })) {
      std::cerr << "cancellable sink did not enter the downstream call\n";
      return 1;
    }
    lock.unlock();
    cancellable.Interrupt();
    const auto completion_deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (cancel_completions.load() == 0U &&
           std::chrono::steady_clock::now() < completion_deadline) {
      std::this_thread::yield();
    }
    if (cancel_completions.load() != 1U || cancel_state->interrupted.load() == 0U) {
      std::cerr << "async sink interrupt did not complete the request as cancelled\n";
      return 1;
    }
  }
  if (std::chrono::steady_clock::now() - cancel_start > std::chrono::milliseconds(600)) {
    std::cerr << "async sink cancellation took too long\n";
    return 1;
  }

  auto dispatch_state = std::make_shared<DispatchGateState>();
  std::mutex dispatch_completion_mutex;
  std::condition_variable dispatch_completion_changed;
  std::uint64_t dispatch_completions = 0U;
  {
    cockpit::voice::AsyncVoiceResponseSink dispatch_guarded(
        std::make_unique<DispatchGateSink>(dispatch_state));
    if (!dispatch_guarded.Submit(
            5U, "cancel before dispatch", [&](const cockpit::voice::VoiceOutputResult& result) {
              std::lock_guard<std::mutex> lock(dispatch_completion_mutex);
              if (result.status == cockpit::voice::VoiceOutputStatus::kCancelled) {
                ++dispatch_completions;
              }
              dispatch_completion_changed.notify_all();
            })) {
      std::cerr << "dispatch-race response was rejected\n";
      return 1;
    }
    {
      std::unique_lock<std::mutex> lock(dispatch_state->mutex);
      if (!dispatch_state->changed.wait_until(
              lock, std::chrono::system_clock::now() + std::chrono::milliseconds(500),
              [dispatch_state] {
                return dispatch_state->before_submit;
              })) {
        std::cerr << "async worker did not reach the pre-dispatch gate\n";
        return 1;
      }
    }
    dispatch_guarded.Interrupt();
    {
      std::unique_lock<std::mutex> lock(dispatch_completion_mutex);
      if (!dispatch_completion_changed.wait_until(
              lock, std::chrono::system_clock::now() + std::chrono::milliseconds(500), [&] {
                return dispatch_completions == 1U;
              })) {
        std::cerr << "pre-dispatch cancellation did not complete\n";
        return 1;
      }
    }
    const auto dispatch_metrics = dispatch_guarded.metrics();
    std::lock_guard<std::mutex> lock(dispatch_state->mutex);
    if (dispatch_state->submit_calls != 0U || dispatch_metrics.queued != 1U ||
        dispatch_metrics.played != 0U || dispatch_metrics.failed != 0U ||
        dispatch_metrics.dropped != 1U) {
      std::cerr << "interrupted output crossed the downstream Submit boundary\n";
      return 1;
    }
  }
  if (dispatch_completions != 1U) {
    std::cerr << "pre-dispatch cancellation completed more than once\n";
    return 1;
  }

  std::mutex failure_mutex;
  std::condition_variable failure_changed;
  bool failure_completed = false;
  cockpit::voice::AsyncVoiceResponseSink failing(std::make_unique<ImmediateFailureSink>());
  if (!failing.Submit(
          6U, "fail asynchronously", [&](const cockpit::voice::VoiceOutputResult& result) {
            std::lock_guard<std::mutex> lock(failure_mutex);
            failure_completed = result.status == cockpit::voice::VoiceOutputStatus::kFailed;
            failure_changed.notify_all();
          })) {
    std::cerr << "failure metrics response was rejected\n";
    return 1;
  }
  {
    std::unique_lock<std::mutex> lock(failure_mutex);
    if (!failure_changed.wait_until(
            lock, std::chrono::system_clock::now() + std::chrono::milliseconds(500), [&] {
              return failure_completed;
            })) {
      std::cerr << "asynchronous playback failure was not completed\n";
      return 1;
    }
  }
  const auto failure_metrics = failing.metrics();
  if (failure_metrics.queued != 1U || failure_metrics.played != 0U ||
      failure_metrics.failed != 1U || failure_metrics.dropped != 0U || !failure_metrics.available) {
    std::cerr << "async wrapper did not own terminal output metrics exactly once\n";
    return 1;
  }
  failing.Stop();

  std::cout << "async voice response sink tests passed\n";
  return 0;
}
