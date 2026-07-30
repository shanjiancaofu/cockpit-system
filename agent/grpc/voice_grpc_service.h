#pragma once

#include <grpcpp/grpcpp.h>

#include <memory>
#include <string>

#include "agent/interaction/voice_interaction_service.h"
#include "voice.grpc.pb.h"

namespace cockpit {
namespace voice {

class VoiceGrpcService final : public proto::voice::VoiceInteractionControl::Service {
 public:
  explicit VoiceGrpcService(VoiceInteractionService& service);
  ~VoiceGrpcService() override;

  bool Start(const std::string& address);
  void Shutdown();

 private:
  grpc::Status GetStatus(grpc::ServerContext* context, const proto::common::Empty* request,
                         proto::voice::VoiceInteractionStatus* response) override;
  grpc::Status ProcessTranscript(grpc::ServerContext* context,
                                 const proto::voice::ProcessTranscriptRequest* request,
                                 proto::voice::VoiceResponseEvent* response) override;
  grpc::Status Interrupt(grpc::ServerContext* context, const proto::common::Empty* request,
                         proto::voice::InterruptVoiceResponse* response) override;
  grpc::Status SubscribeResponses(
      grpc::ServerContext* context, const proto::voice::SubscribeVoiceResponsesRequest* request,
      grpc::ServerWriter<proto::voice::VoiceResponseEvent>* writer) override;
  grpc::Status SubscribeTranscripts(
      grpc::ServerContext* context, const proto::voice::SubscribeTranscriptsRequest* request,
      grpc::ServerWriter<proto::voice::TranscriptEvent>* writer) override;

  static void FillResponse(const VoiceResponse& value, proto::voice::VoiceResponseEvent* response);
  static void FillTranscript(const SpeechTranscript& value,
                             proto::voice::TranscriptEvent* response);
  static void FillStatus(const VoiceInteractionStatus& value,
                         proto::voice::VoiceInteractionStatus* response);

  VoiceInteractionService& service_;
  std::unique_ptr<grpc::Server> server_;
};

}  // namespace voice
}  // namespace cockpit
