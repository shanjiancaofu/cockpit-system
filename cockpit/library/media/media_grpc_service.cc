#include "cockpit/library/media/media_grpc_service.h"

#include "cockpit/core/logging/logger.h"
#include "cockpit/core/time/time.h"

namespace cockpit {
namespace media {
namespace {

proto::media::MediaPlaybackState ToProtoState(MediaPlaybackState state) {
  switch (state) {
    case MediaPlaybackState::kUnavailable:
      return proto::media::MEDIA_PLAYBACK_STATE_UNAVAILABLE;
    case MediaPlaybackState::kStopped:
      return proto::media::MEDIA_PLAYBACK_STATE_STOPPED;
    case MediaPlaybackState::kPlaying:
      return proto::media::MEDIA_PLAYBACK_STATE_PLAYING;
    case MediaPlaybackState::kPaused:
      return proto::media::MEDIA_PLAYBACK_STATE_PAUSED;
    case MediaPlaybackState::kFaulted:
      return proto::media::MEDIA_PLAYBACK_STATE_FAULTED;
  }
  return proto::media::MEDIA_PLAYBACK_STATE_UNSPECIFIED;
}

void FillHealth(const MediaPlaybackStatus& status, proto::common::ServiceHealth* health) {
  health->set_service_name("media-service");
  health->set_checked_at_ms(time::NowMs());
  health->set_last_error(status.last_error);
  if (status.state == MediaPlaybackState::kFaulted) {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_FAULTED);
    health->set_message(status.last_error.empty() ? "media service faulted" : status.last_error);
  } else if (status.state == MediaPlaybackState::kUnavailable) {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_DISABLED);
    health->set_message("media backend disabled");
  } else {
    health->set_state(proto::common::SERVICE_HEALTH_STATE_OK);
    health->set_message("media service online");
  }
}

}  // namespace

MediaGrpcService::MediaGrpcService(MediaService& service) : service_(service) {
}

MediaGrpcService::~MediaGrpcService() {
  Shutdown();
}

bool MediaGrpcService::Start(const std::string& address) {
  grpc::ServerBuilder builder;
  builder.AddListeningPort(address, grpc::InsecureServerCredentials());
  builder.RegisterService(this);
  server_ = builder.BuildAndStart();
  if (server_ == nullptr) {
    LOG_ERROR("failed to start media gRPC server address=" + address);
    return false;
  }
  LOG_INFO("media gRPC server listening address=" + address);
  return true;
}

void MediaGrpcService::Shutdown() {
  if (server_ != nullptr) {
    server_->Shutdown();
    server_.reset();
  }
}

grpc::Status MediaGrpcService::ListTracks(grpc::ServerContext*, const proto::common::Empty*,
                                          proto::media::ListMediaTracksResponse* response) {
  for (const MediaTrack& track : service_.ListTracks()) {
    auto* value = response->add_tracks();
    value->set_id(track.id);
    value->set_title(track.title);
    value->set_artist(track.artist);
    value->set_duration_ms(track.duration_ms);
  }
  return grpc::Status::OK;
}

grpc::Status MediaGrpcService::Play(grpc::ServerContext*,
                                    const proto::media::PlayMediaRequest* request,
                                    proto::media::MediaStatus* response) {
  if (request->track_id().empty()) {
    FillStatus(service_.status(), response);
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "media track id must not be empty");
  }
  std::string error;
  if (!service_.Play(request->track_id(), &error)) {
    FillStatus(service_.status(), response);
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, error);
  }
  FillStatus(service_.status(), response);
  return grpc::Status::OK;
}

grpc::Status MediaGrpcService::Pause(grpc::ServerContext*, const proto::common::Empty*,
                                     proto::media::MediaStatus* response) {
  std::string error;
  if (!service_.Pause(&error)) {
    FillStatus(service_.status(), response);
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, error);
  }
  FillStatus(service_.status(), response);
  return grpc::Status::OK;
}

grpc::Status MediaGrpcService::Resume(grpc::ServerContext*, const proto::common::Empty*,
                                      proto::media::MediaStatus* response) {
  std::string error;
  if (!service_.Resume(&error)) {
    FillStatus(service_.status(), response);
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, error);
  }
  FillStatus(service_.status(), response);
  return grpc::Status::OK;
}

grpc::Status MediaGrpcService::Stop(grpc::ServerContext*, const proto::common::Empty*,
                                    proto::media::MediaStatus* response) {
  std::string error;
  if (!service_.Stop(&error)) {
    FillStatus(service_.status(), response);
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, error);
  }
  FillStatus(service_.status(), response);
  return grpc::Status::OK;
}

grpc::Status MediaGrpcService::Next(grpc::ServerContext*, const proto::common::Empty*,
                                    proto::media::MediaStatus* response) {
  std::string error;
  if (!service_.Next(&error)) {
    FillStatus(service_.status(), response);
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, error);
  }
  FillStatus(service_.status(), response);
  return grpc::Status::OK;
}

grpc::Status MediaGrpcService::GetStatus(grpc::ServerContext*, const proto::common::Empty*,
                                         proto::media::MediaStatus* response) {
  FillStatus(service_.status(), response);
  return grpc::Status::OK;
}

void MediaGrpcService::FillStatus(const MediaPlaybackStatus& status,
                                  proto::media::MediaStatus* response) {
  response->set_state(ToProtoState(status.state));
  response->set_current_track_id(status.current_track_id);
  response->set_title(status.title);
  response->set_artist(status.artist);
  response->set_duration_ms(status.duration_ms);
  response->set_position_ms(status.position_ms);
  response->set_last_error(status.last_error);
  FillHealth(status, response->mutable_health());
}

}  // namespace media
}  // namespace cockpit
