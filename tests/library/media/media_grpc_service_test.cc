#include "cockpit/library/media/media_grpc_service.h"

#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

#include "common.pb.h"
#include "media.grpc.pb.h"

namespace {

void SetDeadline(grpc::ClientContext* context) {
  context->set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));
}

}  // namespace

int main() {
  const std::filesystem::path socket_path =
      "/tmp/cockpit-media-grpc-" + std::to_string(getpid()) + ".sock";
  std::error_code filesystem_error;
  std::filesystem::remove(socket_path, filesystem_error);

  cockpit::media::MediaService service(cockpit::media::CreateMockMediaPlayer());
  cockpit::media::MediaGrpcService grpc_service(service);
  const std::string address = "unix:" + socket_path.string();
  if (!grpc_service.Start(address)) {
    std::cerr << "media gRPC service did not start\n";
    return 1;
  }
  auto stub = cockpit::proto::media::MediaControl::NewStub(
      grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));

  cockpit::proto::common::Empty empty;
  cockpit::proto::media::ListMediaTracksResponse tracks;
  grpc::ClientContext list_context;
  SetDeadline(&list_context);
  if (!stub->ListTracks(&list_context, empty, &tracks).ok() || tracks.tracks_size() != 2) {
    std::cerr << "media gRPC track list mismatch\n";
    return 1;
  }

  cockpit::proto::media::PlayMediaRequest invalid_request;
  invalid_request.set_track_id("../../unsafe.mp3");
  cockpit::proto::media::MediaStatus status;
  grpc::ClientContext invalid_context;
  SetDeadline(&invalid_context);
  if (stub->Play(&invalid_context, invalid_request, &status).error_code() !=
      grpc::StatusCode::FAILED_PRECONDITION) {
    std::cerr << "media gRPC accepted unsafe track id\n";
    return 1;
  }

  cockpit::proto::media::PlayMediaRequest play_request;
  play_request.set_track_id(tracks.tracks(0).id());
  grpc::ClientContext play_context;
  SetDeadline(&play_context);
  if (!stub->Play(&play_context, play_request, &status).ok() ||
      status.state() != cockpit::proto::media::MEDIA_PLAYBACK_STATE_PLAYING ||
      status.health().state() != cockpit::proto::common::SERVICE_HEALTH_STATE_OK) {
    std::cerr << "media gRPC play mismatch\n";
    return 1;
  }

  grpc::ClientContext pause_context;
  SetDeadline(&pause_context);
  if (!stub->Pause(&pause_context, empty, &status).ok() ||
      status.state() != cockpit::proto::media::MEDIA_PLAYBACK_STATE_PAUSED) {
    std::cerr << "media gRPC pause mismatch\n";
    return 1;
  }
  grpc::ClientContext next_context;
  SetDeadline(&next_context);
  if (!stub->Next(&next_context, empty, &status).ok() ||
      status.current_track_id() != tracks.tracks(1).id()) {
    std::cerr << "media gRPC next mismatch\n";
    return 1;
  }
  grpc::ClientContext resume_context;
  SetDeadline(&resume_context);
  if (!stub->Resume(&resume_context, empty, &status).ok() ||
      status.state() != cockpit::proto::media::MEDIA_PLAYBACK_STATE_PLAYING) {
    std::cerr << "media gRPC resume mismatch\n";
    return 1;
  }
  grpc::ClientContext stop_context;
  SetDeadline(&stop_context);
  if (!stub->Stop(&stop_context, empty, &status).ok() ||
      status.state() != cockpit::proto::media::MEDIA_PLAYBACK_STATE_STOPPED) {
    std::cerr << "media gRPC stop mismatch\n";
    return 1;
  }

  grpc_service.Shutdown();
  std::filesystem::remove(socket_path, filesystem_error);
  return 0;
}
