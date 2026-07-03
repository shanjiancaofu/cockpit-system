#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "modules/voice/voice_response_sink.h"

namespace cockpit {
namespace voice {

class AsyncVoiceResponseSink final : public VoiceResponseSink {
 public:
  explicit AsyncVoiceResponseSink(std::unique_ptr<VoiceResponseSink> sink,
                                  std::size_t capacity = 8U);
  ~AsyncVoiceResponseSink() override;

  AsyncVoiceResponseSink(const AsyncVoiceResponseSink&) = delete;
  AsyncVoiceResponseSink& operator=(const AsyncVoiceResponseSink&) = delete;

  bool Submit(std::string text) override;
  VoiceOutputMetrics metrics() const override;
  void Stop() override;

 private:
  void Run();

  const std::unique_ptr<VoiceResponseSink> sink_;
  const std::size_t capacity_;
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  std::deque<std::string> queue_;
  bool stop_requested_ = false;
  std::thread worker_;
  std::atomic<std::uint64_t> queued_{0};
  std::atomic<std::uint64_t> failed_{0};
  std::atomic<std::uint64_t> dropped_{0};
};

}  // namespace voice
}  // namespace cockpit
