#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

#include "agent/llm/local_llm_client.h"

namespace cockpit {
namespace voice {

class LlamaServerLocalLlmClient final : public LocalLlmClient {
 public:
  explicit LlamaServerLocalLlmClient(LocalLlmConfig config);

  LocalLlmResult GenerateResponse(const SpeechTranscript& transcript,
                                  std::chrono::steady_clock::time_point deadline) override;
  void Cancel() override;

 private:
  LocalLlmConfig config_;
  std::atomic<std::uint64_t> cancel_generation_{0};
  std::mutex active_socket_mutex_;
  int active_socket_ = -1;
};

}  // namespace voice
}  // namespace cockpit
