#include "modules/voice/mock_voice_assistant.h"

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
    const SpeechTranscript& transcript) {
  const std::string text = Normalize(transcript.text);
  if (Contains(text, "stop recording")) {
    return {VoiceIntent::kStopRecording, VoiceAction::kStopRecording,
            "Stopping recording."};
  }
  if (Contains(text, "start recording")) {
    return {VoiceIntent::kStartRecording, VoiceAction::kStartRecording,
            "Starting recording."};
  }
  if (Contains(text, "open camera")) {
    return {VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera,
            "Opening the camera."};
  }
  if (Contains(text, "play music")) {
    return {VoiceIntent::kPlayMusic, VoiceAction::kPlayMusic,
            "Starting music playback."};
  }
  if (Contains(text, "vehicle status") || Contains(text, "battery level")) {
    return {VoiceIntent::kShowVehicleStatus,
            VoiceAction::kQueryVehicleStatus,
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
    case VoiceIntent::kStartRecording:
      return "start_recording";
    case VoiceIntent::kStopRecording:
      return "stop_recording";
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
    case VoiceAction::kStartRecording:
      return "start_recording";
    case VoiceAction::kStopRecording:
      return "stop_recording";
  }
  return "none";
}

}  // namespace voice
}  // namespace cockpit
