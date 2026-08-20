#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cockpit {
namespace media {

enum class MediaPlaybackState {
  kUnavailable,
  kStopped,
  kPlaying,
  kPaused,
  kFaulted,
};

struct MediaTrack {
  std::string id;
  std::string title;
  std::string artist;
  std::uint64_t duration_ms = 0;
};

struct MediaPlaybackStatus {
  MediaPlaybackState state = MediaPlaybackState::kUnavailable;
  std::string current_track_id;
  std::string title;
  std::string artist;
  std::uint64_t duration_ms = 0;
  std::uint64_t position_ms = 0;
  std::string last_error;
};

class MediaPlayer {
 public:
  virtual ~MediaPlayer() = default;

  virtual std::vector<MediaTrack> ListTracks() const = 0;
  virtual MediaPlaybackStatus status() const = 0;
  virtual bool Play(const std::string& track_id, std::string* error) = 0;
  virtual bool Pause(std::string* error) = 0;
  virtual bool Resume(std::string* error) = 0;
  virtual bool Stop(std::string* error) = 0;
  virtual bool Next(std::string* error) = 0;
};

std::unique_ptr<MediaPlayer> CreateDisabledMediaPlayer();
std::unique_ptr<MediaPlayer> CreateMockMediaPlayer();

}  // namespace media
}  // namespace cockpit
