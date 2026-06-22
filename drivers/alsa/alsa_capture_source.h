#pragma once

#include "drivers/alsa/alsa_pcm.h"
#include "modules/audio/audio_capture_source.h"

#include <string>

namespace cockpit {
namespace audio {

class AlsaCaptureSource final : public AudioCaptureSource {
 public:
  AlsaCaptureSource(std::string device, PcmFormat format);

  bool Open(std::string* error) override;
  CaptureResult Read(std::int16_t* samples, std::size_t frame_capacity,
                     int timeout_ms,
                     const std::atomic_bool& stop_requested) override;
  void Close() override;

 private:
  const std::string device_;
  const PcmFormat format_;
  AlsaPcm pcm_;
};

}  // namespace audio
}  // namespace cockpit
