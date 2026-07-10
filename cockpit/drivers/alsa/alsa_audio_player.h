#pragma once

#include "cockpit/modules/audio/playback/audio_player.h"

namespace cockpit {
namespace audio {

class AlsaAudioPlayer final : public AudioPlayer {
 public:
  bool Play(const std::string& device, const PcmBuffer& buffer,
            const std::atomic_bool& stop_requested, std::string* error) override;
};

}  // namespace audio
}  // namespace cockpit
