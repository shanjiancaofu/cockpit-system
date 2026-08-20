#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "cockpit/core/base/macros.h"
#include "cockpit/modules/media/media_player.h"

namespace cockpit {
namespace media {

class MediaService final {
 public:
  explicit MediaService(std::unique_ptr<MediaPlayer> player);

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(MediaService);

  std::vector<MediaTrack> ListTracks() const;
  MediaPlaybackStatus status() const;
  bool Play(const std::string& track_id, std::string* error);
  bool Pause(std::string* error);
  bool Resume(std::string* error);
  bool Stop(std::string* error);
  bool Next(std::string* error);

 private:
  const std::unique_ptr<MediaPlayer> player_;
  mutable std::mutex mutex_;
};

}  // namespace media
}  // namespace cockpit
