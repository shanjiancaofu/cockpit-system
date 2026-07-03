#pragma once

#include <chrono>
#include <string>

#include "drivers/alsa/alsa_pcm.h"
#include "modules/audio/capture/audio_capture_source.h"

namespace cockpit {
namespace audio {

class AlsaCaptureSource final : public AudioCaptureSource {
 public:
  AlsaCaptureSource(std::string device, PcmFormat format);

  bool Open(std::string* error) override;
  CaptureResult Read(std::int16_t* samples, std::size_t frame_capacity, int timeout_ms,
                     const std::atomic_bool& stop_requested) override;
  void Close() override;

 private:
  void PaceNullDevice(std::size_t frames_read);

  const std::string device_;
  const PcmFormat format_;
  const bool pace_null_device_;
  std::chrono::steady_clock::time_point next_read_deadline_{};
  AlsaPcm pcm_;
};

}  // namespace audio
}  // namespace cockpit
