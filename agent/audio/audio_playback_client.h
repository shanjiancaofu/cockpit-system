#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>

#include "agent/audio/audio_playback_transport.h"
#include "agent/speech/tts/speech_synthesizer.h"
#include "cockpit/modules/voice/responses/voice_response_sink.h"

namespace cockpit {
namespace voice {

class AudioPlaybackClient final : public VoiceResponseSink {
 public:
  explicit AudioPlaybackClient(const std::string& address);
  AudioPlaybackClient(const std::string& address, std::unique_ptr<SpeechSynthesizer> synthesizer,
                      std::chrono::milliseconds synthesis_timeout = std::chrono::seconds(5));
  AudioPlaybackClient(std::unique_ptr<AudioPlaybackTransport> transport,
                      std::unique_ptr<SpeechSynthesizer> synthesizer,
                      std::chrono::milliseconds synthesis_timeout = std::chrono::seconds(5));

  bool Submit(std::uint64_t request_id, std::string text,
              VoiceOutputCompletion completion) override;
  bool SubmitCancellable(std::uint64_t request_id, std::string text,
                         const std::shared_ptr<const VoiceOutputCancellation>& cancellation,
                         VoiceOutputCompletion completion) override;
  VoiceOutputMetrics metrics() const override;
  void Interrupt() override;
  void Stop() override;

 private:
  void ClearActiveRequest(std::uint64_t request_id);
  void MarkReachable();
  bool RequestPlaybackCancellation(std::uint64_t request_id, std::uint64_t playback_id);
  bool SubmitSingleSegment(std::uint64_t request_id, std::string text,
                           const std::shared_ptr<const VoiceOutputCancellation>& cancellation,
                           VoiceOutputCompletion completion);

  const std::unique_ptr<AudioPlaybackTransport> transport_;
  const std::unique_ptr<SpeechSynthesizer> synthesizer_;
  mutable std::mutex state_mutex_;
  std::condition_variable cancellation_changed_;
  std::uint64_t active_request_id_ = 0;
  std::uint64_t active_playback_id_ = 0;
  std::uint32_t active_cancellation_attempts_ = 0U;
  bool active_cancellation_in_flight_ = false;
  bool active_cancellation_confirmed_ = false;
  bool stopping_ = false;
  const std::chrono::milliseconds synthesis_timeout_;
  std::atomic<std::uint64_t> interrupt_generation_{0};
  std::atomic<std::uint64_t> next_playback_id_{1};
  std::atomic<std::uint64_t> queued_{0};
  std::atomic<std::uint64_t> played_{0};
  std::atomic<std::uint64_t> failed_{0};
  std::atomic<std::uint64_t> dropped_{0};
  std::atomic<std::uint64_t> tts_timeouts_{0};
  std::atomic<std::uint64_t> reconnects_{0};
  std::atomic<std::uint64_t> consecutive_failures_{0};
  std::atomic<std::uint64_t> last_success_timestamp_ms_{0};
  std::atomic<bool> available_{false};
  std::atomic<bool> connected_once_{false};
};

}  // namespace voice
}  // namespace cockpit
