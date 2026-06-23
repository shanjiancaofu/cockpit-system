#pragma once

#include <grpcpp/grpcpp.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "audio.grpc.pb.h"

namespace cockpit {
namespace audio {

class AudioControlClient {
 public:
  using TranscriptHandler = std::function<void(const proto::audio::TranscriptEvent&)>;

  explicit AudioControlClient(const std::string& address);

  bool StartCapture(const std::string& input_device, proto::audio::AudioStatus* status,
                    std::string* error);
  bool StopCapture(proto::audio::AudioStatus* status, std::string* error);
  bool GetStatus(proto::audio::AudioStatus* status, std::string* error);
  bool Speak(const std::string& text, std::string* error);
  bool SubscribeTranscripts(std::uint32_t count, int timeout_ms, const TranscriptHandler& handler,
                            std::string* error);

 private:
  static void SetDeadline(grpc::ClientContext* context);

  std::unique_ptr<proto::audio::AudioControl::Stub> stub_;
};

}  // namespace audio
}  // namespace cockpit
