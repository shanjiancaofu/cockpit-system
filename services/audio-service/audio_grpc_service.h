#pragma once

#include <grpcpp/grpcpp.h>

#include <memory>
#include <string>

#include "audio_service.h"

#include "audio.grpc.pb.h"
#include "modules/voice/responses/voice_response_sink.h"

namespace cockpit {
namespace audio {

class AudioGrpcService final : public proto::audio::AudioControl::Service {
 public:
  AudioGrpcService(AudioService& audio_service, voice::VoiceResponseSink& speech_output);
  ~AudioGrpcService() override;

  AudioGrpcService(const AudioGrpcService&) = delete;
  AudioGrpcService& operator=(const AudioGrpcService&) = delete;

  bool Start(const std::string& address);
  void Shutdown();

 private:
  grpc::Status StartCapture(grpc::ServerContext* context,
                            const proto::audio::StartCaptureRequest* request,
                            proto::audio::AudioStatus* response) override;
  grpc::Status StopCapture(grpc::ServerContext* context, const proto::common::Empty* request,
                           proto::audio::AudioStatus* response) override;
  grpc::Status GetStatus(grpc::ServerContext* context, const proto::common::Empty* request,
                         proto::audio::AudioStatus* response) override;
  grpc::Status Speak(grpc::ServerContext* context, const proto::audio::SpeakRequest* request,
                     proto::audio::SpeakResponse* response) override;
  grpc::Status SubscribeTranscripts(
      grpc::ServerContext* context, const proto::audio::SubscribeTranscriptsRequest* request,
      grpc::ServerWriter<proto::audio::TranscriptEvent>* writer) override;

  void FillStatus(const AudioServiceStatus& status, proto::audio::AudioStatus* response) const;
  static void FillTranscript(const voice::SpeechTranscript& transcript,
                             proto::audio::TranscriptEvent* response);

  AudioService& audio_service_;
  voice::VoiceResponseSink& speech_output_;
  std::unique_ptr<grpc::Server> server_;
};

}  // namespace audio
}  // namespace cockpit
