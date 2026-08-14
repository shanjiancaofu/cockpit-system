#include "cockpit/modules/voice/assistant/mock_voice_assistant.h"

#include <string>

#include "cockpit/modules/voice/assistant/deterministic_command_router.h"
#include "cockpit/modules/voice/assistant/transcript_normalizer.h"

namespace cockpit {
namespace voice {
namespace {

std::string ResponseForRoute(const DeterministicCommandRoute& route) {
  switch (route.action) {
    case VoiceAction::kOpenCamera:
      return "Opening the camera.";
    case VoiceAction::kPlayMusic:
      return "Starting music playback.";
    case VoiceAction::kQueryVehicleStatus:
      return "Retrieving vehicle status.";
    case VoiceAction::kNone:
      return "I could not map that request to a cockpit action.";
  }
  return "I could not map that request to a cockpit action.";
}

}  // namespace

VoiceAssistantResult MockVoiceAssistant::HandleTranscript(
    const SpeechTranscript& transcript, std::chrono::steady_clock::time_point deadline) {
  if (std::chrono::steady_clock::now() >= deadline) {
    return {VoiceIntent::kUnknown, VoiceAction::kNone, "Voice request timed out."};
  }
  const std::string normalized = TranscriptNormalizer::Normalize(transcript.text);
  const DeterministicCommandRoute route = DeterministicCommandRouter().Route(normalized);
  return {route.intent, route.action, ResponseForRoute(route)};
}

const char* ToString(VoiceIntent intent) {
  switch (intent) {
    case VoiceIntent::kUnknown:
      return "unknown";
    case VoiceIntent::kShowVehicleStatus:
      return "show_vehicle_status";
    case VoiceIntent::kOpenCamera:
      return "open_camera";
    case VoiceIntent::kPlayMusic:
      return "play_music";
  }
  return "unknown";
}

const char* ToString(VoiceAction action) {
  switch (action) {
    case VoiceAction::kNone:
      return "none";
    case VoiceAction::kQueryVehicleStatus:
      return "query_vehicle_status";
    case VoiceAction::kOpenCamera:
      return "open_camera";
    case VoiceAction::kPlayMusic:
      return "play_music";
  }
  return "none";
}

}  // namespace voice
}  // namespace cockpit
