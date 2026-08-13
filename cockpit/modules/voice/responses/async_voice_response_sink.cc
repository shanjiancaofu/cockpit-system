#include "cockpit/modules/voice/responses/async_voice_response_sink.h"

#include <exception>
#include <stdexcept>
#include <utility>

namespace cockpit {
namespace voice {

struct AsyncVoiceResponseSink::MetricsState {
  std::atomic<std::uint64_t> queued{0};
  std::atomic<std::uint64_t> played{0};
  std::atomic<std::uint64_t> failed{0};
  std::atomic<std::uint64_t> dropped{0};
};

struct AsyncVoiceResponseSink::PendingOutput {
  std::uint64_t request_id = 0;
  std::string text;
  VoiceOutputCompletion completion;
  std::shared_ptr<VoiceOutputCancellation> cancellation =
      std::make_shared<VoiceOutputCancellation>();
  std::atomic_bool completed{false};
};

AsyncVoiceResponseSink::AsyncVoiceResponseSink(std::unique_ptr<VoiceResponseSink> sink,
                                               std::size_t capacity)
    : sink_(std::move(sink)), capacity_(capacity), metrics_(std::make_shared<MetricsState>()) {
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
    pending.swap(queue_);
    if (active_ != nullptr) {
      active_->cancellation->RequestCancellation();
    }
  }
  if (stopping) {
    sink_->Stop();
  } else {
    sink_->Interrupt();
  }
  for (const auto& output : pending) {
    output->cancellation->RequestCancellation();
    Complete(output, metrics_, VoiceOutputStatus::kCancelled, "voice output interrupted");
  }
  changed_.notify_all();
}

bool AsyncVoiceResponseSink::Submit(std::uint64_t request_id, std::string text,
                                    VoiceOutputCompletion completion) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (stop_requested_ || request_id == 0U || text.empty() || !completion ||
      queue_.size() >= capacity_) {
    metrics_->dropped.fetch_add(1U);
    return false;
  }
  auto output = std::make_shared<PendingOutput>();
  output->request_id = request_id;
  output->text = std::move(text);
  output->completion = std::move(completion);
  queue_.push_back(std::move(output));
  metrics_->queued.fetch_add(1U);
  changed_.notify_one();
  return true;
}

VoiceOutputMetrics AsyncVoiceResponseSink::metrics() const {
  const VoiceOutputMetrics sink_metrics = sink_->metrics();
  VoiceOutputMetrics result;
  // This wrapper owns lifecycle totals for every request it accepts. The downstream sink only
  // contributes transport-health fields, so an asynchronous failure cannot be missed or counted
  // once by each layer.
  result.queued = metrics_->queued.load();
  result.played = metrics_->played.load();
  result.failed = metrics_->failed.load();
  result.dropped = metrics_->dropped.load();
  result.tts_timeouts = sink_metrics.tts_timeouts;
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
      const bool accepted = sink_->SubmitCancellable(
          output->request_id, std::move(output->text), output->cancellation,
          [output, metrics = metrics_](VoiceOutputResult result) {
            Complete(output, metrics,
                     output->cancellation->IsCancellationRequested() ? VoiceOutputStatus::kCancelled
                                                                     : result.status,
                     std::move(result.error));
          });
      if (!accepted) {
        const bool interrupted = output->cancellation->IsCancellationRequested();
        Complete(output, metrics_,
                 interrupted ? VoiceOutputStatus::kCancelled : VoiceOutputStatus::kFailed,
                 interrupted ? "voice output interrupted" : "voice output was rejected");
      }
    } catch (const std::exception&) {
      Complete(output, metrics_, VoiceOutputStatus::kFailed, "voice output failed");
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
                                      const std::shared_ptr<MetricsState>& metrics,
                                      VoiceOutputStatus status, std::string error) {
  if (output == nullptr || output->completed.exchange(true)) {
    return;
  }
  switch (status) {
    case VoiceOutputStatus::kCompleted:
      metrics->played.fetch_add(1U);
      break;
    case VoiceOutputStatus::kFailed:
      metrics->failed.fetch_add(1U);
      break;
    case VoiceOutputStatus::kCancelled:
    case VoiceOutputStatus::kDropped:
      metrics->dropped.fetch_add(1U);
      break;
  }
  output->completion({output->request_id, status, std::move(error)});
}

}  // namespace voice
}  // namespace cockpit
