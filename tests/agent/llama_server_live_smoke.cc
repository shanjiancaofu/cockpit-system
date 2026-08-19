#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

#include "agent/llm/llama_server_local_llm_client.h"

int main(int argc, char** argv) {
  if (argc != 4 && argc != 5) {
    std::cerr << "usage: llama_server_live_smoke HOST PORT MODEL [PROMPT]\n";
    return 2;
  }

  cockpit::voice::LocalLlmConfig config;
  config.provider = "llama-server";
  config.host = argv[1];
  const int port = std::stoi(argv[2]);
  if (port < 1 || port > 65535) {
    std::cerr << "invalid llama-server port\n";
    return 2;
  }
  config.port = static_cast<std::uint16_t>(port);
  config.model = argv[3];
  config.max_tokens = 64;
  config.temperature = 0.0;
  config.first_token_timeout = std::chrono::seconds(30);

  cockpit::voice::LlamaServerLocalLlmClient client(std::move(config));
  cockpit::voice::SpeechTranscript transcript;
  transcript.text = argc == 5 ? argv[4] : "Reply with exactly: cockpit ready";
  const auto result = client.GenerateResponse(
      transcript, std::chrono::steady_clock::now() + std::chrono::seconds(60));
  if (!result.success || result.response_text.empty()) {
    std::cerr << "local LLM request failed: " << result.error << '\n';
    return 1;
  }
  std::cout << result.response_text << '\n';
  std::cout << "first_content_ms=" << result.first_content_latency.count() << '\n';
  std::cout << "total_response_ms=" << result.total_latency.count() << '\n';
  return 0;
}
