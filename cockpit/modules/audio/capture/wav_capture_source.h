#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "cockpit/modules/audio/capture/audio_capture_source.h"
#include "cockpit/modules/audio/frames/pcm_format.h"

namespace cockpit {
namespace audio {

class WavCaptureSource final : public AudioCaptureSource {
 public:
  WavCaptureSource(std::filesystem::path path, PcmFormat expected_format);

  bool Open(std::string* error) override;
  CaptureResult Read(std::int16_t* samples, std::size_t frame_capacity, int timeout_ms,
                     const std::atomic_bool& stop_requested) override;
  void Close() override;

 private:
  const std::filesystem::path path_;
  const PcmFormat expected_format_;
  std::vector<std::int16_t> samples_;
  std::size_t offset_{0};
  std::chrono::steady_clock::time_point next_read_deadline_{};
};

}  // namespace audio
}  // namespace cockpit
