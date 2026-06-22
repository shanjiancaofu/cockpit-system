#include "modules/voice/mock_action_dispatcher.h"
#include "modules/voice/mock_voice_assistant.h"

#include <iostream>
#include <string>

namespace {

bool Expect(const std::string& text, cockpit::voice::VoiceIntent intent,
            cockpit::voice::VoiceAction action) {
  cockpit::voice::MockVoiceAssistant assistant;
  cockpit::voice::SpeechTranscript transcript;
  transcript.text = text;
  const auto result = assistant.HandleTranscript(transcript);
  return result.intent == intent && result.action == action &&
         !result.response_text.empty();
}

}  // namespace

int main() {
  using cockpit::voice::VoiceAction;
  using cockpit::voice::VoiceIntent;
  if (!Expect("Show vehicle status", VoiceIntent::kShowVehicleStatus,
              VoiceAction::kQueryVehicleStatus) ||
      !Expect("OPEN CAMERA", VoiceIntent::kOpenCamera,
              VoiceAction::kOpenCamera) ||
      !Expect("play music", VoiceIntent::kPlayMusic,
              VoiceAction::kPlayMusic) ||
      !Expect("start recording", VoiceIntent::kStartRecording,
              VoiceAction::kStartRecording) ||
      !Expect("stop recording", VoiceIntent::kStopRecording,
              VoiceAction::kStopRecording) ||
      !Expect("tell me a joke", VoiceIntent::kUnknown, VoiceAction::kNone)) {
    std::cerr << "mock voice assistant intent mapping failed\n";
    return 1;
  }
  cockpit::voice::MockActionDispatcher dispatcher;
  if (dispatcher.Execute(VoiceAction::kOpenCamera).status !=
          cockpit::voice::ActionExecutionStatus::kSucceeded ||
      dispatcher.Execute(VoiceAction::kNone).status !=
          cockpit::voice::ActionExecutionStatus::kNotRequested ||
      dispatcher.Execute(static_cast<VoiceAction>(999)).status !=
          cockpit::voice::ActionExecutionStatus::kRejected) {
    std::cerr << "mock action dispatcher policy failed\n";
    return 1;
  }
  std::cout << "mock voice assistant tests passed\n";
  return 0;
}
