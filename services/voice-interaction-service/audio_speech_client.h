#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "audio.grpc.pb.h"
#include "modules/voice/voice_response_sink.h"

namespace cockpit {
namespace voice {

class AudioSpeechClient final : public VoiceResponseSink {
 public:
  explicit AudioSpeechClient(const std::string& address);

  bool Submit(std::string text) override;
  VoiceOutputMetrics metrics() const override;

 private:
  std::unique_ptr<proto::audio::AudioControl::Stub> stub_;
  std::atomic<std::uint64_t> queued_{0};
  std::atomic<std::uint64_t> failed_{0};
  std::atomic<std::uint64_t> dropped_{0};
};

}  // namespace voice
}  // namespace cockpit
