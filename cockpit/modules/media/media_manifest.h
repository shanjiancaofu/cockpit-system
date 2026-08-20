#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "cockpit/modules/media/media_player.h"

namespace cockpit {
namespace media {

struct MediaManifestTrack {
  MediaTrack metadata;
  std::filesystem::path file;
  std::string sha256;
};

class MediaManifest final {
 public:
  static bool Load(const std::filesystem::path& manifest_path, MediaManifest* manifest,
                   std::string* error);

  const std::vector<MediaManifestTrack>& tracks() const {
    return tracks_;
  }

 private:
  std::vector<MediaManifestTrack> tracks_;
};

}  // namespace media
}  // namespace cockpit
