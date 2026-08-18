#include "cockpit/modules/audio/capture/wav_capture_source.h"

#include <algorithm>
#include <thread>
#include <utility>

#include "cockpit/modules/audio/wav/wav_file.h"

namespace cockpit {
namespace audio {

WavCaptureSource::WavCaptureSource(std::filesystem::path path, PcmFormat expected_format)
    : path_(std::move(path)), expected_format_(expected_format) {
}

bool WavCaptureSource::Open(std::string* error) {
  PcmBuffer buffer;
  if (!ReadPcm16Wav(path_.string(), &buffer, error)) {
    return false;
  }
  if (buffer.format.sample_rate_hz != expected_format_.sample_rate_hz ||
      buffer.format.channels != expected_format_.channels) {
    if (error != nullptr) {
      *error = "WAV capture source format does not match configured audio format";
    }
    return false;
  }
  samples_ = std::move(buffer.samples);
  offset_ = 0;
  next_read_deadline_ = std::chrono::steady_clock::now();
  return true;
}

CaptureResult WavCaptureSource::Read(std::int16_t* samples, std::size_t frame_capacity,
                                     int /*timeout_ms*/, const std::atomic_bool& stop_requested) {
  if (stop_requested.load()) {
    return {CaptureStatus::kStopped, 0, 0, {}};
  }
  if (offset_ >= samples_.size()) {
    return {CaptureStatus::kStopped, 0, 0, {}};
  }
  const std::size_t count = std::min(frame_capacity, samples_.size() - offset_);
  std::copy_n(samples_.data() + offset_, count, samples);
  offset_ += count;

  next_read_deadline_ += std::chrono::nanoseconds(
      static_cast<std::int64_t>(count) * 1'000'000'000LL / expected_format_.sample_rate_hz);
  std::this_thread::sleep_until(next_read_deadline_);
  return {CaptureStatus::kOk, count, 0, {}};
}

void WavCaptureSource::Close() {
  samples_.clear();
  offset_ = 0;
}

}  // namespace audio
}  // namespace cockpit
