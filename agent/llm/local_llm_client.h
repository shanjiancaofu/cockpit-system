#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "cockpit/modules/voice/assistant/speech_transcript.h"

namespace cockpit {
namespace voice {

struct LocalLlmResult {
  bool success = false;
  std::string response_text;
  std::string provider;
  std::string error;
};

struct LocalLlmConfig {
  std::string provider = "mock";
  std::string host = "127.0.0.1";
  std::uint16_t port = 8080;
  std::string path = "/v1/chat/completions";
  std::string model = "Qwen3.5-2B";
  std::string system_prompt = "You are a cockpit assistant.";
  std::size_t max_tokens = 128;
  double temperature = 0.2;
  std::chrono::milliseconds first_token_timeout{5000};
};

class LocalLlmClient {
 public:
  virtual ~LocalLlmClient() = default;

  virtual LocalLlmResult GenerateResponse(const SpeechTranscript& transcript,
                                          std::chrono::steady_clock::time_point deadline) = 0;
  virtual void Cancel() = 0;
};

}  // namespace voice
}  // namespace cockpit
