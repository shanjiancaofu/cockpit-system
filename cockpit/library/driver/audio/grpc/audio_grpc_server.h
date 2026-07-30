#pragma once

#include <grpcpp/grpcpp.h>

#include <memory>
#include <string>

#include "audio.grpc.pb.h"
#include "cockpit/core/base/macros.h"
#include "cockpit/library/driver/audio/capture/audio_capture_controller.h"
#include "cockpit/library/driver/audio/playback/audio_playback.h"

namespace cockpit {
namespace audio {

class AudioGrpcServer final : public proto::audio::AudioControl::Service {
 public:
  AudioGrpcServer(AudioCaptureController& capture, AudioPlayback& playback);
  ~AudioGrpcServer() override;

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(AudioGrpcServer);

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
  grpc::Status PlayPcm(grpc::ServerContext* context, const proto::audio::PlayPcmRequest* request,
                       proto::audio::PlayPcmResponse* response) override;

  void FillStatus(const AudioCaptureControllerStatus& status,
                  proto::audio::AudioStatus* response) const;

  AudioCaptureController& capture_;
  AudioPlayback& playback_;
  std::unique_ptr<grpc::Server> server_;
};

}  // namespace audio
}  // namespace cockpit
