#include "cockpit/modules/media/media_player.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace cockpit {
namespace media {
namespace {

void AssignError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

void ClearError(std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
}

class DisabledMediaPlayer final : public MediaPlayer {
 public:
  std::vector<MediaTrack> ListTracks() const override {
    return {};
  }
  MediaPlaybackStatus status() const override {
    return {MediaPlaybackState::kUnavailable, {}, {}, {}, 0, 0, "media backend is disabled"};
  }
  bool Play(const std::string&, std::string* error) override {
    return Reject(error);
  }
  bool Pause(std::string* error) override {
    return Reject(error);
  }
  bool Resume(std::string* error) override {
    return Reject(error);
  }
  bool Stop(std::string* error) override {
    return Reject(error);
  }
  bool Next(std::string* error) override {
    return Reject(error);
  }

 private:
  static bool Reject(std::string* error) {
    AssignError(error, "media backend is disabled");
    return false;
  }
};

class MockMediaPlayer final : public MediaPlayer {
 public:
  MockMediaPlayer()
      : tracks_{{"mock_track_one", "Mock Track One", "Cockpit Fixture", 180000},
                {"mock_track_two", "Mock Track Two", "Cockpit Fixture", 210000}} {
    status_.state = MediaPlaybackState::kStopped;
  }

  std::vector<MediaTrack> ListTracks() const override {
    return tracks_;
  }

  MediaPlaybackStatus status() const override {
    return status_;
  }

  bool Play(const std::string& track_id, std::string* error) override {
    // The UI uses a stable default_track command ID. Keep the fixture's concrete IDs private to
    // this provider while explicitly mapping that one allowlisted command to its first track.
    const std::string resolved_track_id =
        track_id == "default_track" ? tracks_.front().id : track_id;
    const auto found =
        std::find_if(tracks_.begin(), tracks_.end(), [&resolved_track_id](const MediaTrack& track) {
          return track.id == resolved_track_id;
        });
    if (found == tracks_.end()) {
      AssignError(error, "media track id is not allowlisted");
      return false;
    }
    current_index_ = static_cast<std::size_t>(std::distance(tracks_.begin(), found));
    status_.state = MediaPlaybackState::kPlaying;
    status_.position_ms = 0;
    status_.last_error.clear();
    ApplyTrack(*found);
    ClearError(error);
    return true;
  }

  bool Pause(std::string* error) override {
    if (status_.state != MediaPlaybackState::kPlaying) {
      AssignError(error, "media is not playing");
      return false;
    }
    status_.state = MediaPlaybackState::kPaused;
    ClearError(error);
    return true;
  }

  bool Resume(std::string* error) override {
    if (status_.state != MediaPlaybackState::kPaused) {
      AssignError(error, "media is not paused");
      return false;
    }
    status_.state = MediaPlaybackState::kPlaying;
    ClearError(error);
    return true;
  }

  bool Stop(std::string* error) override {
    status_.state = MediaPlaybackState::kStopped;
    status_.position_ms = 0;
    ClearError(error);
    return true;
  }

  bool Next(std::string* error) override {
    if (status_.state != MediaPlaybackState::kPlaying &&
        status_.state != MediaPlaybackState::kPaused) {
      AssignError(error, "media must be playing or paused to select next track");
      return false;
    }
    const MediaPlaybackState state = status_.state;
    current_index_ = (current_index_ + 1U) % tracks_.size();
    status_.position_ms = 0;
    ApplyTrack(tracks_[current_index_]);
    status_.state = state;
    ClearError(error);
    return true;
  }

 private:
  void ApplyTrack(const MediaTrack& track) {
    status_.current_track_id = track.id;
    status_.title = track.title;
    status_.artist = track.artist;
    status_.duration_ms = track.duration_ms;
  }

  const std::vector<MediaTrack> tracks_;
  std::size_t current_index_ = 0;
  MediaPlaybackStatus status_;
};

}  // namespace

std::unique_ptr<MediaPlayer> CreateDisabledMediaPlayer() {
  return std::make_unique<DisabledMediaPlayer>();
}

std::unique_ptr<MediaPlayer> CreateMockMediaPlayer() {
  return std::make_unique<MockMediaPlayer>();
}

}  // namespace media
}  // namespace cockpit
