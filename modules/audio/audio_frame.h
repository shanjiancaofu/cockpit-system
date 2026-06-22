#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace cockpit {
namespace audio {

enum class AudioFrameFlag : std::uint32_t {
  kNone = 0,
  kDiscontinuity = 1U << 0U,
  kRecoveredFromXrun = 1U << 1U,
  kDroppedBefore = 1U << 2U,
};

constexpr AudioFrameFlag operator|(AudioFrameFlag left, AudioFrameFlag right) {
  return static_cast<AudioFrameFlag>(static_cast<std::uint32_t>(left) |
                                     static_cast<std::uint32_t>(right));
}

class AudioFrame {
 public:
  static constexpr std::uint32_t kSampleRateHz = 16000;
  static constexpr std::uint32_t kChannels = 1;
  static constexpr std::uint32_t kFrameMs = 20;
  static constexpr std::size_t kSampleCount = kSampleRateHz * kFrameMs / 1000U;
  using Samples = std::array<std::int16_t, kSampleCount>;

  AudioFrame(std::uint64_t sequence, std::int64_t capture_time_ns, AudioFrameFlag flags,
             Samples samples);

  AudioFrame(const AudioFrame&) = default;
  AudioFrame(AudioFrame&&) noexcept = default;
  AudioFrame& operator=(const AudioFrame&) = delete;
  AudioFrame& operator=(AudioFrame&&) = delete;

  std::uint64_t sequence() const { return sequence_; }
  std::int64_t capture_time_ns() const { return capture_time_ns_; }
  AudioFrameFlag flags() const { return flags_; }
  const Samples& samples() const { return samples_; }
  bool HasFlag(AudioFrameFlag flag) const;

 private:
  const std::uint64_t sequence_;
  const std::int64_t capture_time_ns_;
  const AudioFrameFlag flags_;
  const Samples samples_;
};

}  // namespace audio
}  // namespace cockpit
