#include "agent/audio/audio_focus_controller.h"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include "cockpit/core/logging/logger.h"
#include "common.pb.h"
#include "media.grpc.pb.h"

namespace cockpit {
namespace voice {
namespace {

class MediaAudioFocusController final : public AudioFocusController {
 public:
  MediaAudioFocusController(const std::string& address, std::chrono::milliseconds timeout)
      : timeout_(timeout) {
    grpc::ChannelArguments arguments;
    arguments.SetInt(GRPC_ARG_ENABLE_HTTP_PROXY, 0);
    stub_ = proto::media::MediaControl::NewStub(
        grpc::CreateCustomChannel(address, grpc::InsecureChannelCredentials(), arguments));
  }

  bool AcquireTts() override {
    if (stub_ == nullptr || acquired_) {
      return false;
    }
    proto::common::Empty request;
    proto::media::MediaStatus status;
    grpc::ClientContext status_context;
    SetDeadline(&status_context);
    const grpc::Status status_rpc = stub_->GetStatus(&status_context, request, &status);
    if (!status_rpc.ok()) {
      LOG_WARN("audio focus acquire GetStatus failed: " + status_rpc.error_message());
      return false;
    }
    switch (status.state()) {
      case proto::media::MEDIA_PLAYBACK_STATE_PLAYING: {
        LOG_INFO("audio focus observed media state=playing");
        proto::media::MediaStatus paused;
        grpc::ClientContext pause_context;
        SetDeadline(&pause_context);
        const grpc::Status pause_rpc = stub_->Pause(&pause_context, request, &paused);
        if (!pause_rpc.ok() || paused.state() != proto::media::MEDIA_PLAYBACK_STATE_PAUSED) {
          LOG_WARN("audio focus acquire failed to confirm media state=paused");
          return false;
        }
        resume_needed_ = true;
        acquired_ = true;
        LOG_INFO("audio focus acquire confirmed media state=paused");
        return true;
      }
      case proto::media::MEDIA_PLAYBACK_STATE_PAUSED:
        acquired_ = true;
        resume_needed_ = false;
        LOG_INFO("audio focus observed media already paused; resume not owned");
        return true;
      case proto::media::MEDIA_PLAYBACK_STATE_STOPPED:
      case proto::media::MEDIA_PLAYBACK_STATE_UNAVAILABLE:
        acquired_ = true;
        resume_needed_ = false;
        LOG_INFO("audio focus acquired with no competing media playback");
        return true;
      case proto::media::MEDIA_PLAYBACK_STATE_FAULTED:
      case proto::media::MEDIA_PLAYBACK_STATE_UNSPECIFIED:
      default:
        LOG_WARN("audio focus acquire rejected unhealthy media state");
        return false;
    }
  }

  void ReleaseTts() override {
    if (!acquired_ || stub_ == nullptr) {
      return;
    }
    if (resume_needed_) {
      proto::common::Empty request;
      proto::media::MediaStatus playing;
      grpc::ClientContext context;
      SetDeadline(&context);
      const grpc::Status rpc = stub_->Resume(&context, request, &playing);
      if (rpc.ok() && playing.state() == proto::media::MEDIA_PLAYBACK_STATE_PLAYING) {
        LOG_INFO("audio focus release confirmed media state=playing");
      } else {
        LOG_WARN("audio focus release failed to confirm media state=playing");
      }
    }
    resume_needed_ = false;
    acquired_ = false;
  }

 private:
  void SetDeadline(grpc::ClientContext* context) const {
    context->set_wait_for_ready(true);
    context->set_deadline(std::chrono::system_clock::now() + timeout_);
  }

  const std::chrono::milliseconds timeout_;
  std::unique_ptr<proto::media::MediaControl::Stub> stub_;
  bool acquired_ = false;
  bool resume_needed_ = false;
};

}  // namespace

std::unique_ptr<AudioFocusController> CreateMediaAudioFocusController(
    const std::string& address, std::chrono::milliseconds timeout) {
  if (address.empty() || timeout <= std::chrono::milliseconds::zero() ||
      timeout > std::chrono::seconds(1)) {
    return nullptr;
  }
  return std::make_unique<MediaAudioFocusController>(address, timeout);
}

}  // namespace voice
}  // namespace cockpit
