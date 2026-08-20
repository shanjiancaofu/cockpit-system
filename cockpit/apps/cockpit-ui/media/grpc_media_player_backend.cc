#include "cockpit/apps/cockpit-ui/media/grpc_media_player_backend.h"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <utility>

#include "common.pb.h"
#include "media.grpc.pb.h"

namespace cockpit {
namespace ui {
namespace {

constexpr auto kRpcTimeout = std::chrono::milliseconds(500);

void SetDeadline(grpc::ClientContext* context) {
  context->set_wait_for_ready(true);
  context->set_deadline(std::chrono::system_clock::now() + kRpcTimeout);
}

MediaPlaybackState ToUiState(proto::media::MediaPlaybackState state) {
  switch (state) {
    case proto::media::MEDIA_PLAYBACK_STATE_STOPPED:
      return MediaPlaybackState::kStopped;
    case proto::media::MEDIA_PLAYBACK_STATE_PLAYING:
      return MediaPlaybackState::kPlaying;
    case proto::media::MEDIA_PLAYBACK_STATE_PAUSED:
      return MediaPlaybackState::kPaused;
    case proto::media::MEDIA_PLAYBACK_STATE_FAULTED:
      return MediaPlaybackState::kError;
    case proto::media::MEDIA_PLAYBACK_STATE_UNAVAILABLE:
    case proto::media::MEDIA_PLAYBACK_STATE_UNSPECIFIED:
    default:
      return MediaPlaybackState::kUnavailable;
  }
}

std::string StatusMessage(const proto::media::MediaStatus& status) {
  if (!status.last_error().empty()) {
    return status.last_error();
  }
  if (!status.health().message().empty()) {
    return status.health().message();
  }
  return {};
}

MediaBackendStatus ToBackendStatus(const proto::media::MediaStatus& status) {
  const MediaPlaybackState state = ToUiState(status.state());
  return {state != MediaPlaybackState::kUnavailable,
          state,
          status.current_track_id(),
          status.title(),
          status.artist(),
          StatusMessage(status)};
}

std::string RpcError(const grpc::Status& status) {
  if (status.ok()) {
    return {};
  }
  if (!status.error_message().empty()) {
    return status.error_message();
  }
  return "media service RPC failed (code=" + std::to_string(status.error_code()) + ")";
}

MediaBackendResult ToResult(const grpc::Status& rpc_status,
                            const proto::media::MediaStatus& status) {
  const MediaBackendStatus mapped = ToBackendStatus(status);
  const std::string error = RpcError(rpc_status);
  if (!rpc_status.ok()) {
    return {false,
            mapped.state == MediaPlaybackState::kUnavailable ? MediaPlaybackState::kError
                                                             : mapped.state,
            mapped.track_id,
            mapped.title,
            mapped.artist,
            error.empty() ? mapped.message : error};
  }
  if (mapped.state == MediaPlaybackState::kUnavailable ||
      mapped.state == MediaPlaybackState::kError) {
    return {false, mapped.state, mapped.track_id, mapped.title, mapped.artist, mapped.message};
  }
  return {true, mapped.state, mapped.track_id, mapped.title, mapped.artist, mapped.message};
}

class GrpcMediaPlayerBackend final : public MediaPlayerBackend {
 public:
  explicit GrpcMediaPlayerBackend(std::string address) : address_(std::move(address)) {
  }

  MediaBackendStatus Query() override {
    proto::common::Empty request;
    proto::media::MediaStatus response;
    grpc::ClientContext context;
    SetDeadline(&context);
    const grpc::Status status = Stub()->GetStatus(&context, request, &response);
    if (!status.ok()) {
      return {false, MediaPlaybackState::kUnavailable, {}, {}, {}, RpcError(status)};
    }
    return ToBackendStatus(response);
  }

  MediaBackendResult Play(const std::string& track_id) override {
    proto::media::PlayMediaRequest request;
    request.set_track_id(track_id);
    proto::media::MediaStatus response;
    grpc::ClientContext context;
    SetDeadline(&context);
    return ToResult(Stub()->Play(&context, request, &response), response);
  }

  MediaBackendResult Pause() override {
    return UnaryStatusCall(&proto::media::MediaControl::Stub::Pause);
  }
  MediaBackendResult Resume() override {
    return UnaryStatusCall(&proto::media::MediaControl::Stub::Resume);
  }
  MediaBackendResult Stop() override {
    return UnaryStatusCall(&proto::media::MediaControl::Stub::Stop);
  }
  MediaBackendResult Next() override {
    return UnaryStatusCall(&proto::media::MediaControl::Stub::Next);
  }

 private:
  using UnaryMethod = grpc::Status (proto::media::MediaControl::Stub::*)(
      grpc::ClientContext*, const proto::common::Empty&, proto::media::MediaStatus*);

  proto::media::MediaControl::Stub* Stub() {
    if (stub_ == nullptr) {
      grpc::ChannelArguments arguments;
      arguments.SetInt(GRPC_ARG_ENABLE_HTTP_PROXY, 0);
      channel_ = grpc::CreateCustomChannel(address_, grpc::InsecureChannelCredentials(), arguments);
      stub_ = proto::media::MediaControl::NewStub(channel_);
    }
    return stub_.get();
  }

  MediaBackendResult UnaryStatusCall(UnaryMethod method) {
    proto::common::Empty request;
    proto::media::MediaStatus response;
    grpc::ClientContext context;
    SetDeadline(&context);
    return ToResult((Stub()->*method)(&context, request, &response), response);
  }

  const std::string address_;
  std::shared_ptr<grpc::Channel> channel_;
  std::unique_ptr<proto::media::MediaControl::Stub> stub_;
};

}  // namespace

std::unique_ptr<MediaPlayerBackend> CreateGrpcMediaPlayerBackend(std::string address) {
  return std::make_unique<GrpcMediaPlayerBackend>(std::move(address));
}

}  // namespace ui
}  // namespace cockpit
