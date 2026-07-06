#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cockpit/modules/audio/frames/pcm_format.h"

namespace cockpit {
namespace audio {

struct PcmBuffer {
  PcmFormat format;
  std::vector<std::int16_t> samples;

  std::size_t FrameCount() const;
};

bool WritePcm16Wav(const std::string& path, const PcmFormat& format,
                   const std::vector<std::int16_t>& samples, std::string* error = nullptr);
bool ReadPcm16Wav(const std::string& path, PcmBuffer* buffer, std::string* error = nullptr);

}  // namespace audio
}  // namespace cockpit
