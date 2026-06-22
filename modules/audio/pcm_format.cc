#include "modules/audio/pcm_format.h"

namespace cockpit {
namespace audio {
namespace {

bool Fail(const std::string& message, std::string* error) {
  if (error != nullptr) {
    *error = message;
  }
  return false;
}

}  // namespace

bool PcmFormat::IsValid(std::string* error) const {
  if (sample_rate_hz < 8000 || sample_rate_hz > 192000) {
    return Fail("sample_rate_hz must be between 8000 and 192000", error);
  }
  if (channels < 1 || channels > 8) {
    return Fail("channels must be between 1 and 8", error);
  }
  if (frame_ms < 1 || frame_ms > 100) {
    return Fail("frame_ms must be between 1 and 100", error);
  }
  if (FramesPerPeriod() == 0) {
    return Fail("audio period must contain at least one frame", error);
  }
  if (sample_format != PcmSampleFormat::kSigned16LittleEndian) {
    return Fail("unsupported PCM sample format", error);
  }
  return true;
}

std::size_t PcmFormat::BytesPerSample() const {
  return sample_format == PcmSampleFormat::kSigned16LittleEndian ? 2U : 0U;
}

std::size_t PcmFormat::BytesPerFrame() const {
  return BytesPerSample() * static_cast<std::size_t>(channels);
}

std::size_t PcmFormat::FramesPerPeriod() const {
  return static_cast<std::size_t>(sample_rate_hz) * static_cast<std::size_t>(frame_ms) / 1000U;
}

}  // namespace audio
}  // namespace cockpit
