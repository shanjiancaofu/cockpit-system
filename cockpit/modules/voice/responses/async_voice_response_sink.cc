#include "cockpit/modules/voice/responses/async_voice_response_sink.h"

#include <exception>
#include <stdexcept>
#include <utility>

namespace cockpit {
namespace voice {

struct AsyncVoiceResponseSink::PendingOutput {
  std::uint64_t request_id = 0;
  std::string text;
  VoiceOutputCompletion completion;
  std::atomic_bool completed{false};
  std::atomic_bool interrupted{false};
};

AsyncVoiceResponseSink::AsyncVoiceResponseSink(std::unique_ptr<VoiceResponseSink> sink,
                                               std::size_t capacity)
    : sink_(std::move(sink)), capacity_(capacity) {
  if (sink_ == nullptr || capacity_ == 0) {
    throw std::invalid_argument("async voice output requires a sink and positive capacity");
  }
  worker_ = std::thread(&AsyncVoiceResponseSink::Run, this);
}

AsyncVoiceResponseSink::~AsyncVoiceResponseSink() {
  Stop();
}

void AsyncVoiceResponseSink::Stop() {
  CancelPending(true);
  if (worker_.joinable()) {
    worker_.join();
  }
}

void AsyncVoiceResponseSink::Interrupt() {
  CancelPending(false);
}

void AsyncVoiceResponseSink::CancelPending(bool stopping) {
  std::deque<std::shared_ptr<PendingOutput>> pending;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stop_requested_) {
      return;
    }
    if (stopping) {
      stop_requested_ = true;
    }
    dropped_.fetch_add(static_cast<std::uint64_t>(queue_.size()));
    pending.swap(queue_);
    if (active_ != nullptr) {
      active_->interrupted.store(true);
    }
  }
  for (const auto& output : pending) {
    output->interrupted.store(true);
    Complete(output, VoiceOutputStatus::kCancelled, "voice output interrupted");
  }
  if (stopping) {
    sink_->Stop();
  } else {
    sink_->Interrupt();
  }
  changed_.notify_all();
}

bool AsyncVoiceResponseSink::Submit(std::uint64_t request_id, std::string text,
                                    VoiceOutputCompletion completion) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (stop_requested_ || request_id == 0U || text.empty() || !completion ||
      queue_.size() >= capacity_) {
    dropped_.fetch_add(1U);
    return false;
  }
  auto output = std::make_shared<PendingOutput>();
  output->request_id = request_id;
  output->text = std::move(text);
  output->completion = std::move(completion);
  queue_.push_back(std::move(output));
  queued_.fetch_add(1U);
  changed_.notify_one();
  return true;
}

VoiceOutputMetrics AsyncVoiceResponseSink::metrics() const {
  const VoiceOutputMetrics sink_metrics = sink_->metrics();
  VoiceOutputMetrics result;
  result.queued = queued_.load();
  result.played = sink_metrics.played;
  result.failed = failed_.load();
  result.dropped = dropped_.load();
  result.reconnects = sink_metrics.reconnects;
  result.consecutive_failures = sink_metrics.consecutive_failures;
  result.last_success_timestamp_ms = sink_metrics.last_success_timestamp_ms;
  result.available = sink_metrics.available;
  return result;
}

void AsyncVoiceResponseSink::Run() {
  while (true) {
    std::shared_ptr<PendingOutput> output;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      changed_.wait(lock, [this] {
        return stop_requested_ || !queue_.empty();
      });
      if (stop_requested_) {
        break;
      }
      output = std::move(queue_.front());
      queue_.pop_front();
      active_ = output;
    }

    try {
      const bool accepted = sink_->Submit(
          output->request_id, std::move(output->text), [output](VoiceOutputResult result) {
            Complete(output,
                     output->interrupted.load() ? VoiceOutputStatus::kCancelled : result.status,
                     std::move(result.error));
          });
      if (!accepted) {
        failed_.fetch_add(1U);
        Complete(
            output,
            output->interrupted.load() ? VoiceOutputStatus::kCancelled : VoiceOutputStatus::kFailed,
            output->interrupted.load() ? "voice output interrupted" : "voice output was rejected");
      }
    } catch (const std::exception&) {
      failed_.fetch_add(1U);
      Complete(output, VoiceOutputStatus::kFailed, "voice output failed");
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (active_ == output) {
        active_.reset();
      }
    }
  }
}

void AsyncVoiceResponseSink::Complete(const std::shared_ptr<PendingOutput>& output,
                                      VoiceOutputStatus status, std::string error) {
  if (output == nullptr || output->completed.exchange(true)) {
    return;
  }
  output->completion({output->request_id, status, std::move(error)});
}

}  // namespace voice
}  // namespace cockpit
