#pragma once

#include <grpcpp/grpcpp.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "voice.grpc.pb.h"

namespace cockpit {
namespace voice {

class VoiceControlClient {
 public:
  using ResponseHandler = std::function<void(const proto::voice::VoiceResponseEvent&)>;

  explicit VoiceControlClient(const std::string& address);

  bool GetStatus(proto::voice::VoiceInteractionStatus* status, std::string* error);
  bool ProcessTranscript(const std::string& text, proto::voice::VoiceResponseEvent* response,
                         std::string* error);
  bool SubscribeResponses(std::uint64_t after_id, std::uint32_t count, int timeout_ms,
                          const ResponseHandler& handler, std::string* error);

 private:
  static void SetDeadline(grpc::ClientContext* context);

  std::unique_ptr<proto::voice::VoiceInteractionControl::Stub> stub_;
};

}  // namespace voice
}  // namespace cockpit
