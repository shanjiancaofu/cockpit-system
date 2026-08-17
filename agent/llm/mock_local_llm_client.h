#pragma once

#include <string>

#include "agent/llm/local_llm_client.h"

namespace cockpit {
namespace voice {

class MockLocalLlmClient final : public LocalLlmClient {
 public:
  explicit MockLocalLlmClient(std::string prefix = "Local answer: ");

  LocalLlmResult GenerateResponse(const SpeechTranscript& transcript,
                                  std::chrono::steady_clock::time_point deadline) override;
  void Cancel() override {
  }

 private:
  std::string prefix_;
};

}  // namespace voice
}  // namespace cockpit
