#include "cockpit/library/media/media_service.h"

#include <iostream>
#include <string>

#include "cockpit/modules/media/media_player.h"

namespace {

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  cockpit::media::MediaService disabled(cockpit::media::CreateDisabledMediaPlayer());
  std::string error;
  if (!Check(disabled.status().state == cockpit::media::MediaPlaybackState::kUnavailable,
             "disabled media state mismatch") ||
      !Check(disabled.ListTracks().empty(), "disabled media exposed tracks") ||
      !Check(!disabled.Play("mock_track_one", &error) && !error.empty(),
             "disabled media accepted playback")) {
    return 1;
  }

  cockpit::media::MediaService service(cockpit::media::CreateMockMediaPlayer());
  const auto tracks = service.ListTracks();
  if (!Check(tracks.size() == 2, "mock media track list mismatch") ||
      !Check(service.status().state == cockpit::media::MediaPlaybackState::kStopped,
             "mock media initial state mismatch") ||
      !Check(
          !service.Play("../../unsafe.mp3", &error) && error == "media track id is not allowlisted",
          "unsafe media id was accepted") ||
      !Check(service.Play(tracks.front().id, &error), "mock media did not play") ||
      !Check(service.status().state == cockpit::media::MediaPlaybackState::kPlaying,
             "mock media playing state mismatch") ||
      !Check(service.Pause(&error), "mock media did not pause") ||
      !Check(service.Next(&error), "mock media did not select next track") ||
      !Check(service.status().state == cockpit::media::MediaPlaybackState::kPaused &&
                 service.status().current_track_id == tracks.back().id,
             "mock media next track did not preserve pause") ||
      !Check(service.Resume(&error), "mock media did not resume") ||
      !Check(service.Stop(&error), "mock media did not stop") ||
      !Check(service.status().state == cockpit::media::MediaPlaybackState::kStopped,
             "mock media final state mismatch")) {
    return 1;
  }
  return 0;
}
