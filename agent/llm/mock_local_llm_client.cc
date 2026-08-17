#include "agent/llm/mock_local_llm_client.h"

#include <chrono>
#include <utility>

namespace cockpit {
namespace voice {

MockLocalLlmClient::MockLocalLlmClient(std::string prefix) : prefix_(std::move(prefix)) {
}

LocalLlmResult MockLocalLlmClient::GenerateResponse(
    const SpeechTranscript& transcript, std::chrono::steady_clock::time_point deadline) {
  if (std::chrono::steady_clock::now() >= deadline) {
    return {false, {}, "mock-local-llm", "local llm deadline exceeded"};
  }
  return {true, prefix_ + transcript.text, "mock-local-llm", {}};
}

}  // namespace voice
}  // namespace cockpit
