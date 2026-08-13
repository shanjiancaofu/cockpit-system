#include "cockpit/modules/voice/assistant/mock_voice_assistant.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace cockpit {
namespace voice {
namespace {

std::string Normalize(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char value) {
    return static_cast<char>(std::tolower(value));
  });
  return text;
}

bool Contains(const std::string& text, const std::string& phrase) {
  return text.find(phrase) != std::string::npos;
}

}  // namespace

VoiceAssistantResult MockVoiceAssistant::HandleTranscript(
    const SpeechTranscript& transcript, std::chrono::steady_clock::time_point deadline) {
  if (std::chrono::steady_clock::now() >= deadline) {
    return {VoiceIntent::kUnknown, VoiceAction::kNone, "Voice request timed out."};
  }
  const std::string text = Normalize(transcript.text);
  if (Contains(text, "open camera")) {
    return {VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera, "Opening the camera."};
  }
  if (Contains(text, "play music")) {
    return {VoiceIntent::kPlayMusic, VoiceAction::kPlayMusic, "Starting music playback."};
  }
  if (Contains(text, "vehicle status") || Contains(text, "battery level")) {
    return {VoiceIntent::kShowVehicleStatus, VoiceAction::kQueryVehicleStatus,
            "Retrieving vehicle status."};
  }
  return {VoiceIntent::kUnknown, VoiceAction::kNone,
          "I could not map that request to a cockpit action."};
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
