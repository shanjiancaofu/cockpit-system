#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace cockpit {
namespace voice {

struct VoiceOutputMetrics {
  std::uint64_t queued = 0;
  std::uint64_t played = 0;
  std::uint64_t failed = 0;
  std::uint64_t dropped = 0;
  std::uint64_t tts_timeouts = 0;
  std::uint64_t reconnects = 0;
  std::uint64_t consecutive_failures = 0;
  std::uint64_t last_success_timestamp_ms = 0;
  bool available = false;
};

enum class VoiceOutputStatus {
  kCompleted,
  kFailed,
  kCancelled,
  kDropped,
};

struct VoiceOutputResult {
  std::uint64_t request_id = 0;
  VoiceOutputStatus status = VoiceOutputStatus::kFailed;
  std::string error;
};

using VoiceOutputCompletion = std::function<void(VoiceOutputResult)>;

class VoiceOutputCancellation {
 public:
  bool IsCancellationRequested() const {
    return requested_.load();
  }

  void RequestCancellation() {
    requested_.store(true);
  }

 private:
  std::atomic_bool requested_{false};
};

class VoiceResponseSink {
 public:
  virtual ~VoiceResponseSink() = default;

  // A true return accepts ownership of completion and guarantees exactly one callback.
  virtual bool Submit(std::uint64_t request_id, std::string text,
                      VoiceOutputCompletion completion) = 0;
  // Async dispatchers use this entry point so cancellation is checked by the component that
  // commits the downstream work, rather than only by the queue that dequeued it.
  virtual bool SubmitCancellable(std::uint64_t request_id, std::string text,
                                 const std::shared_ptr<const VoiceOutputCancellation>& cancellation,
                                 VoiceOutputCompletion completion) {
    if (cancellation != nullptr && cancellation->IsCancellationRequested()) {
      return false;
    }
    return Submit(request_id, std::move(text), std::move(completion));
  }
  virtual VoiceOutputMetrics metrics() const = 0;
  virtual void Interrupt() {
  }
  virtual void Stop() {
  }
};

}  // namespace voice
}  // namespace cockpit
