#include "cockpit/modules/voice/assistant/deterministic_command_router.h"

#include <array>
#include <string_view>

namespace cockpit {
namespace voice {
namespace {

struct CommandEntry {
  std::string_view text;
  VoiceIntent intent;
  VoiceAction action;
};

constexpr std::array<CommandEntry, 33> kCommands{{
    {"打开相机", VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera},
    {"请打开相机", VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera},
    {"帮我打开相机", VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera},
    {"开相机", VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera},
    {"请开相机", VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera},
    {"帮我开相机", VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera},
    {"打开摄像头", VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera},
    {"请打开摄像头", VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera},
    {"帮我打开摄像头", VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera},
    {"开摄像头", VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera},
    {"请开摄像头", VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera},
    {"帮我开摄像头", VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera},
    {"open camera", VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera},
    {"please open camera", VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera},
    {"open the camera", VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera},
    {"please open the camera", VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera},
    {"播放音乐", VoiceIntent::kPlayMusic, VoiceAction::kPlayMusic},
    {"请播放音乐", VoiceIntent::kPlayMusic, VoiceAction::kPlayMusic},
    {"帮我播放音乐", VoiceIntent::kPlayMusic, VoiceAction::kPlayMusic},
    {"放音乐", VoiceIntent::kPlayMusic, VoiceAction::kPlayMusic},
    {"请放音乐", VoiceIntent::kPlayMusic, VoiceAction::kPlayMusic},
    {"play music", VoiceIntent::kPlayMusic, VoiceAction::kPlayMusic},
    {"please play music", VoiceIntent::kPlayMusic, VoiceAction::kPlayMusic},
    {"车辆状态", VoiceIntent::kShowVehicleStatus, VoiceAction::kQueryVehicleStatus},
    {"查看车辆状态", VoiceIntent::kShowVehicleStatus, VoiceAction::kQueryVehicleStatus},
    {"请查看车辆状态", VoiceIntent::kShowVehicleStatus, VoiceAction::kQueryVehicleStatus},
    {"查看电量", VoiceIntent::kShowVehicleStatus, VoiceAction::kQueryVehicleStatus},
    {"请查看电量", VoiceIntent::kShowVehicleStatus, VoiceAction::kQueryVehicleStatus},
    {"电量多少", VoiceIntent::kShowVehicleStatus, VoiceAction::kQueryVehicleStatus},
    {"vehicle status", VoiceIntent::kShowVehicleStatus, VoiceAction::kQueryVehicleStatus},
    {"battery level", VoiceIntent::kShowVehicleStatus, VoiceAction::kQueryVehicleStatus},
    {"show vehicle status", VoiceIntent::kShowVehicleStatus, VoiceAction::kQueryVehicleStatus},
    {"show battery level", VoiceIntent::kShowVehicleStatus, VoiceAction::kQueryVehicleStatus},
}};

}  // namespace

DeterministicCommandRoute DeterministicCommandRouter::Route(
    std::string_view normalized_text) const {
  for (const CommandEntry& command : kCommands) {
    if (normalized_text == command.text) {
      return {command.intent, command.action};
    }
  }
  return {};
}

}  // namespace voice
}  // namespace cockpit
