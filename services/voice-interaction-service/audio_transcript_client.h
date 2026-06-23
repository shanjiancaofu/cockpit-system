#pragma once

#include <functional>
#include <memory>
#include <string>

#include "modules/voice/speech_transcript.h"

namespace cockpit {
namespace voice {

class AudioTranscriptClient {
 public:
  using TranscriptHandler = std::function<void(const SpeechTranscript&)>;
  using ContinueHandler = std::function<bool()>;
  using ReconnectHandler = std::function<void()>;
  using ErrorHandler = std::function<void(const std::string&)>;

  AudioTranscriptClient(std::string address, int stream_timeout_ms, int retry_delay_ms);

  int Stream(const TranscriptHandler& transcript_handler, const ContinueHandler& should_continue,
             const ReconnectHandler& reconnect_handler, const ErrorHandler& error_handler);

 private:
  std::string address_;
  int stream_timeout_ms_;
  int retry_delay_ms_;
};

}  // namespace voice
}  // namespace cockpit
