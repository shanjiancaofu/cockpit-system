#pragma once

#include <cstddef>
#include <string>

namespace cockpit {
namespace audio {

enum class PcmSampleFormat {
  kSigned16LittleEndian,
};

struct PcmFormat {
  int sample_rate_hz = 16000;
  int channels = 1;
  int frame_ms = 20;
  PcmSampleFormat sample_format = PcmSampleFormat::kSigned16LittleEndian;

  bool IsValid(std::string* error = nullptr) const;
  std::size_t BytesPerSample() const;
  std::size_t BytesPerFrame() const;
  std::size_t FramesPerPeriod() const;
};

}  // namespace audio
}  // namespace cockpit
