#include "modules/voice/async_voice_response_sink.h"

#include <exception>
#include <stdexcept>
#include <utility>

namespace cockpit {
namespace voice {

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
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stop_requested_) {
      return;
    }
    stop_requested_ = true;
    dropped_.fetch_add(static_cast<std::uint64_t>(queue_.size()));
    queue_.clear();
  }
  sink_->Stop();
  changed_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

bool AsyncVoiceResponseSink::Submit(std::string text) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (stop_requested_ || text.empty() || queue_.size() >= capacity_) {
    dropped_.fetch_add(1U);
    return false;
  }
  queue_.push_back(std::move(text));
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
  result.available = sink_metrics.available;
  return result;
}

void AsyncVoiceResponseSink::Run() {
  while (true) {
    std::string text;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      changed_.wait(lock, [this] {
        return stop_requested_ || !queue_.empty();
      });
      if (stop_requested_) {
        break;
      }
      text = std::move(queue_.front());
      queue_.pop_front();
    }

    try {
      if (!sink_->Submit(std::move(text))) {
        failed_.fetch_add(1U);
      }
    } catch (const std::exception&) {
      failed_.fetch_add(1U);
    }
  }
}

}  // namespace voice
}  // namespace cockpit
