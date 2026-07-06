#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include "audio.grpc.pb.h"
#include "cockpit/modules/voice/responses/voice_response_sink.h"

namespace cockpit {
namespace voice {

class AudioSpeechClient final : public VoiceResponseSink {
 public:
  explicit AudioSpeechClient(const std::string& address);

  bool Submit(std::string text) override;
  VoiceOutputMetrics metrics() const override;
  void Stop() override;

 private:
  void MarkReachable();

  std::unique_ptr<proto::audio::AudioControl::Stub> stub_;
  mutable std::mutex context_mutex_;
  grpc::ClientContext* active_context_ = nullptr;
  bool stopping_ = false;
  std::atomic<std::uint64_t> queued_{0};
  std::atomic<std::uint64_t> failed_{0};
  std::atomic<std::uint64_t> dropped_{0};
  std::atomic<std::uint64_t> reconnects_{0};
  std::atomic<std::uint64_t> consecutive_failures_{0};
  std::atomic<std::uint64_t> last_success_timestamp_ms_{0};
  std::atomic<bool> available_{false};
  std::atomic<bool> connected_once_{false};
};

}  // namespace voice
}  // namespace cockpit
