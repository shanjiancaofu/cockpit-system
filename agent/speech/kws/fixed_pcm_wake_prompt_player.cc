#include "agent/speech/kws/fixed_pcm_wake_prompt_player.h"

#include <stdexcept>
#include <utility>

namespace cockpit {
namespace agent {
namespace {

audio::PcmBuffer MakeWakeCue() {
  audio::PcmBuffer buffer;
  buffer.format.sample_rate_hz = 16000;
  buffer.format.channels = 1;
  constexpr std::size_t kDurationMs = 120;
  constexpr std::size_t kSampleRate = 16000;
  constexpr std::size_t kSamples = kSampleRate * kDurationMs / 1000U;
  constexpr std::size_t kHalfPeriodSamples = 18;
  constexpr std::int16_t kAmplitude = 4500;
  buffer.samples.resize(kSamples);
  for (std::size_t index = 0; index < buffer.samples.size(); ++index) {
    const bool high = (index / kHalfPeriodSamples) % 2U == 0U;
    buffer.samples[index] = high ? kAmplitude : -kAmplitude;
  }
  return buffer;
}

void SetError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

}  // namespace

FixedPcmWakePromptPlayer::FixedPcmWakePromptPlayer(
    std::unique_ptr<voice::AudioPlaybackTransport> transport)
    : transport_(std::move(transport)) {
  if (transport_ == nullptr) {
    throw std::invalid_argument("fixed wake prompt requires an audio playback transport");
  }
}

bool FixedPcmWakePromptPlayer::Play(std::string* error) {
  const std::uint64_t playback_id = next_playback_id_++;
  const voice::AudioPlaybackSubmitResult submitted = transport_->Submit(playback_id, MakeWakeCue());
  if (submitted.status != voice::AudioPlaybackSubmitStatus::kAccepted) {
    SetError(error,
             submitted.error.empty() ? "wake prompt playback was rejected" : submitted.error);
    return false;
  }
  const voice::AudioPlaybackWaitResult result = transport_->Wait(playback_id, timeout_);
  if (result.status == voice::AudioPlaybackWaitStatus::kCompleted) {
    if (error != nullptr) {
      error->clear();
    }
    return true;
  }
  SetError(error, result.error.empty() ? "wake prompt playback did not complete" : result.error);
  transport_->Cancel(playback_id);
  return false;
}

}  // namespace agent
}  // namespace cockpit
