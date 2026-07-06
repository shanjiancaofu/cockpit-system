#pragma once

#include <string>

#include "cockpit/modules/audio/wav/wav_file.h"

namespace cockpit {
namespace audio {

class AudioPlayer {
 public:
  virtual ~AudioPlayer() = default;

  virtual bool Play(const std::string& device, const PcmBuffer& buffer, std::string* error) = 0;
};

}  // namespace audio
}  // namespace cockpit
