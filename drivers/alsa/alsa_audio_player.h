#pragma once

#include "modules/audio/audio_player.h"

namespace cockpit {
namespace audio {

class AlsaAudioPlayer final : public AudioPlayer {
 public:
  bool Play(const std::string& device, const PcmBuffer& buffer, std::string* error) override;
};

}  // namespace audio
}  // namespace cockpit
