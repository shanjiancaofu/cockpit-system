#pragma once

#include "audio.grpc.pb.h"
#include "audio_service.h"

#include <grpcpp/grpcpp.h>

#include <memory>
#include <string>

namespace cockpit {
namespace audio {

class AudioGrpcService final : public proto::audio::AudioControl::Service {
 public:
  explicit AudioGrpcService(AudioService& audio_service);
  ~AudioGrpcService() override;

  AudioGrpcService(const AudioGrpcService&) = delete;
  AudioGrpcService& operator=(const AudioGrpcService&) = delete;

  bool Start(const std::string& address);
  void Shutdown();

 private:
  grpc::Status StartCapture(grpc::ServerContext* context,
                            const proto::audio::StartCaptureRequest* request,
                            proto::audio::AudioStatus* response) override;
  grpc::Status StopCapture(grpc::ServerContext* context,
                           const proto::common::Empty* request,
                           proto::audio::AudioStatus* response) override;
  grpc::Status GetStatus(grpc::ServerContext* context,
                         const proto::common::Empty* request,
                         proto::audio::AudioStatus* response) override;
  grpc::Status SubscribeTranscripts(
      grpc::ServerContext* context,
      const proto::audio::SubscribeTranscriptsRequest* request,
      grpc::ServerWriter<proto::audio::TranscriptEvent>* writer) override;

  static void FillStatus(const AudioServiceStatus& status,
                         proto::audio::AudioStatus* response);
  static void FillTranscript(const voice::SpeechTranscript& transcript,
                             proto::audio::TranscriptEvent* response);

  AudioService& audio_service_;
  std::unique_ptr<grpc::Server> server_;
};

}  // namespace audio
}  // namespace cockpit
