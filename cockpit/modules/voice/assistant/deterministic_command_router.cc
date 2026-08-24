#include "cockpit/modules/voice/assistant/deterministic_command_router.h"

#include <array>
#include <string>
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

bool ContainsAny(std::string_view text, const std::initializer_list<std::string_view> values) {
  for (const std::string_view value : values) {
    if (text.find(value) != std::string_view::npos) {
      return true;
    }
  }
  return false;
}

bool ContainsEnglishPhrase(std::string_view text, std::string_view phrase) {
  std::size_t offset = text.find(phrase);
  while (offset != std::string_view::npos) {
    const bool left_boundary =
        offset == 0U || (text[offset - 1U] < 'a' || text[offset - 1U] > 'z') &&
                            (text[offset - 1U] < 'A' || text[offset - 1U] > 'Z');
    const std::size_t end = offset + phrase.size();
    const bool right_boundary = end == text.size() || (text[end] < 'a' || text[end] > 'z') &&
                                                          (text[end] < 'A' || text[end] > 'Z');
    if (left_boundary && right_boundary) {
      return true;
    }
    offset = text.find(phrase, offset + 1U);
  }
  return false;
}

bool HasNegativeMusicPhrase(std::string_view text) {
  return ContainsAny(text, {"不要", "别", "不播放", "请勿", "不用", "不需要", "不能", "禁止",
                            "don't", "do not", "never", "can't", "cannot", "not play"});
}

bool HasMusicRequest(std::string_view text) {
  return ContainsAny(text, {"播放音乐", "放音乐"}) || ContainsEnglishPhrase(text, "play music") ||
         ContainsEnglishPhrase(text, "please play music");
}

}  // namespace

DeterministicCommandRoute DeterministicCommandRouter::Route(
    std::string_view normalized_text) const {
  for (const CommandEntry& command : kCommands) {
    if (normalized_text == command.text) {
      return {command.intent, command.action};
    }
  }
  // ASR may repeat or lightly paraphrase one intent, for example
  // "打开音乐，播放音乐，play music". Accept that only when no other allowlisted
  // action or negative phrase is present; mixed actions remain fail-closed.
  if (HasMusicRequest(normalized_text) && !HasNegativeMusicPhrase(normalized_text) &&
      !ContainsAny(normalized_text, {"相机", "摄像头", "camera", "车辆状态", "电量",
                                     "vehicle status", "battery level"})) {
    return {VoiceIntent::kPlayMusic, VoiceAction::kPlayMusic};
  }
  return {};
}

}  // namespace voice
}  // namespace cockpit
