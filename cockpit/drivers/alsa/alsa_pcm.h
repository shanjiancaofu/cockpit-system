#pragma once

#include <alsa/asoundlib.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cockpit/core/base/macros.h"
#include "cockpit/modules/audio/capture/audio_capture_source.h"
#include "cockpit/modules/audio/frames/pcm_format.h"

namespace cockpit {
namespace audio {

enum class PcmDirection {
  kCapture,
  kPlayback,
};

enum class AlsaDeviceIo {
  kInput,
  kOutput,
  kDuplex,
  kUnknown,
};

struct AlsaDeviceInfo {
  std::string name;
  std::string description;
  AlsaDeviceIo io = AlsaDeviceIo::kUnknown;
};

class AlsaPcm {
 public:
  AlsaPcm() = default;
  ~AlsaPcm();

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(AlsaPcm);
  AlsaPcm(AlsaPcm&& other) noexcept;
  AlsaPcm& operator=(AlsaPcm&& other) noexcept;

  static std::vector<AlsaDeviceInfo> ListDevices(std::string* error = nullptr);

  bool Open(const std::string& device, PcmDirection direction, const PcmFormat& format,
            std::string* error = nullptr);
  CaptureResult PollReadFrames(std::int16_t* samples, std::size_t frame_capacity, int timeout_ms,
                               const std::atomic_bool& stop_requested);
  bool WriteFrames(const std::int16_t* samples, std::size_t frame_count,
                   std::string* error = nullptr, const std::atomic_bool* stop_requested = nullptr);
  bool Drain(std::string* error = nullptr, const std::atomic_bool* stop_requested = nullptr);
  void Close();

  bool IsOpen() const {
    return handle_ != nullptr;
  }
  const PcmFormat& format() const {
    return format_;
  }
  PcmDirection direction() const {
    return direction_;
  }

 private:
  bool Recover(int alsa_error, const std::string& operation, std::string* error);

  snd_pcm_t* handle_ = nullptr;
  PcmFormat format_;
  PcmDirection direction_ = PcmDirection::kCapture;
};

const char* ToString(AlsaDeviceIo io);

}  // namespace audio
}  // namespace cockpit
