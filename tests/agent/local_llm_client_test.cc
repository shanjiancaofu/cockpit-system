#include <chrono>
#include <iostream>
#include <memory>

#include "agent/interaction/voice_interaction_service.h"
#include "agent/llm/mock_local_llm_client.h"
#include "cockpit/modules/voice/actions/mock_action_dispatcher.h"
#include "cockpit/modules/voice/assistant/mock_voice_assistant.h"

namespace {

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return false;
  }
  return true;
}

}  // namespace

int main() {
  cockpit::voice::MockLocalLlmClient client("Local answer: ");
  cockpit::voice::SpeechTranscript transcript;
  transcript.text = "tell me a joke";
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  const auto result = client.GenerateResponse(transcript, deadline);
  if (!Check(result.success, "mock local LLM did not succeed") ||
      !Check(result.response_text == "Local answer: tell me a joke",
             "mock local LLM response text mismatch")) {
    return 1;
  }

  cockpit::voice::VoiceInteractionService service(
      true, std::make_unique<cockpit::voice::MockVoiceAssistant>(),
      std::make_unique<cockpit::voice::MockActionDispatcher>(), nullptr, nullptr,
      std::chrono::seconds(1), std::chrono::seconds(1), std::chrono::seconds(1),
      std::make_unique<cockpit::voice::MockLocalLlmClient>("LLM reply: "));

  cockpit::voice::SpeechTranscript unknown_transcript;
  unknown_transcript.text = "what can you do";
  const auto unknown_response = service.HandleTranscript(unknown_transcript);
  if (!Check(unknown_response.has_value(), "unknown transcript did not return a response") ||
      !Check(unknown_response->intent == cockpit::voice::VoiceIntent::kUnknown,
             "unknown transcript changed intent") ||
      !Check(unknown_response->action == cockpit::voice::VoiceAction::kNone,
             "unknown transcript changed action") ||
      !Check(unknown_response->response_text == "LLM reply: what can you do",
             "unknown transcript did not use local LLM response")) {
    return 1;
  }

  cockpit::voice::SpeechTranscript command_transcript;
  command_transcript.text = "open camera";
  const auto command_response = service.HandleTranscript(command_transcript);
  if (!Check(command_response.has_value(), "command transcript did not return a response") ||
      !Check(command_response->action == cockpit::voice::VoiceAction::kOpenCamera,
             "command transcript stopped using deterministic action") ||
      !Check(command_response->response_text == "Mock action completed: open_camera",
             "command transcript response did not come from the action dispatcher")) {
    return 1;
  }

  std::cout << "local LLM client tests passed\n";
  return 0;
}
