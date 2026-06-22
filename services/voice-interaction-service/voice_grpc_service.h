#pragma once

#include "voice.grpc.pb.h"
#include "voice_interaction_service.h"

#include <grpcpp/grpcpp.h>

#include <memory>
#include <string>

namespace cockpit {
namespace voice {

class VoiceGrpcService final
    : public proto::voice::VoiceInteractionControl::Service {
 public:
  explicit VoiceGrpcService(VoiceInteractionService& service);
  ~VoiceGrpcService() override;

  bool Start(const std::string& address);
  void Shutdown();

 private:
  grpc::Status GetStatus(
      grpc::ServerContext* context, const proto::common::Empty* request,
      proto::voice::VoiceInteractionStatus* response) override;
  grpc::Status ProcessTranscript(
      grpc::ServerContext* context,
      const proto::voice::ProcessTranscriptRequest* request,
      proto::voice::VoiceResponseEvent* response) override;
  grpc::Status SubscribeResponses(
      grpc::ServerContext* context,
      const proto::voice::SubscribeVoiceResponsesRequest* request,
      grpc::ServerWriter<proto::voice::VoiceResponseEvent>* writer) override;

  static void FillResponse(const VoiceResponse& value,
                           proto::voice::VoiceResponseEvent* response);
  static void FillStatus(const VoiceInteractionStatus& value,
                         proto::voice::VoiceInteractionStatus* response);

  VoiceInteractionService& service_;
  std::unique_ptr<grpc::Server> server_;
};

}  // namespace voice
}  // namespace cockpit
