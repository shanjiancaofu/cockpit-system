#pragma once

#include <alsa/asoundlib.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cockpit {
namespace audio {

enum class PcmDirection : std::uint8_t {
  kCapture,
  kPlayback,
};

struct AlsaPcmFormat {
  int sample_rate_hz = 16000;
  int channels = 1;
  int frame_ms = 20;

  bool IsValid() const;
  std::size_t FramesPerPeriod() const;
};

enum class AlsaReadStatus : std::uint8_t {
  kOk,
  kTimeout,
  kXrunRecovered,
  kStopped,
  kDeviceError,
};

struct AlsaReadResult {
  AlsaReadStatus status = AlsaReadStatus::kDeviceError;
  std::size_t frames_read = 0;
  int device_error = 0;
  std::string message;
};

enum class AlsaDeviceIo : std::uint8_t {
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

  AlsaPcm(const AlsaPcm&) = delete;
  AlsaPcm& operator=(const AlsaPcm&) = delete;
  AlsaPcm(AlsaPcm&& other) noexcept;
  AlsaPcm& operator=(AlsaPcm&& other) noexcept;

  static std::vector<AlsaDeviceInfo> ListDevices(std::string* error = nullptr);

  bool Open(const std::string& device, PcmDirection direction, const AlsaPcmFormat& format,
            std::string* error = nullptr);
  AlsaReadResult PollReadFrames(std::int16_t* samples, std::size_t frame_capacity, int timeout_ms,
                                const std::atomic_bool& stop_requested);
  bool WriteFrames(const std::int16_t* samples, std::size_t frame_count,
                   std::string* error = nullptr, const std::atomic_bool* stop_requested = nullptr);
  bool Drain(std::string* error = nullptr, const std::atomic_bool* stop_requested = nullptr);
  void Close();

  bool IsOpen() const {
    return handle_ != nullptr;
  }
  const AlsaPcmFormat& format() const {
    return format_;
  }
  PcmDirection direction() const {
    return direction_;
  }

 private:
  bool Recover(int alsa_error, const std::string& operation, std::string* error);

  snd_pcm_t* handle_ = nullptr;
  AlsaPcmFormat format_;
  PcmDirection direction_ = PcmDirection::kCapture;
};

const char* ToString(AlsaDeviceIo io);

}  // namespace audio
}  // namespace cockpit
