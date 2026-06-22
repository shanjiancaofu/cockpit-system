#pragma once

#include "modules/audio/wav_file.h"

#include <string>

namespace cockpit {
namespace audio {

class AudioPlayer {
 public:
  virtual ~AudioPlayer() = default;

  virtual bool Play(const std::string& device, const PcmBuffer& buffer,
                    std::string* error) = 0;
};

}  // namespace audio
}  // namespace cockpit
