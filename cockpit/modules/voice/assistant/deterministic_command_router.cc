#include "cockpit/modules/voice/assistant/deterministic_command_router.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <string_view>

namespace cockpit {
namespace voice {
namespace {

bool IsAsciiWordCharacter(char value) {
  const auto ascii = static_cast<unsigned char>(value);
  return std::isalnum(ascii) != 0 || value == '_';
}

bool ContainsEnglishPhrase(std::string_view text, std::string_view phrase) {
  std::size_t position = text.find(phrase);
  while (position != std::string_view::npos) {
    const std::size_t end = position + phrase.size();
    const bool starts_at_boundary = position == 0 || !IsAsciiWordCharacter(text[position - 1]);
    const bool ends_at_boundary = end == text.size() || !IsAsciiWordCharacter(text[end]);
    if (starts_at_boundary && ends_at_boundary) {
      return true;
    }
    position = text.find(phrase, position + 1);
  }
  return false;
}

bool ContainsPhrase(std::string_view text, std::string_view phrase) {
  return static_cast<unsigned char>(phrase.front()) < 0x80U
             ? ContainsEnglishPhrase(text, phrase)
             : text.find(phrase) != std::string_view::npos;
}

template <std::size_t Size>
bool ContainsAny(std::string_view text, const std::array<std::string_view, Size>& phrases) {
  for (const std::string_view phrase : phrases) {
    if (ContainsPhrase(text, phrase)) {
      return true;
    }
  }
  return false;
}

constexpr std::array<std::string_view, 4> kNegations{"don't", "do not", "不要", "别"};

constexpr std::array<std::string_view, 8> kUnsupportedVehicleParameters{
    "set speed", "steering", "throttle", "速度", "方向盘", "转向", "油门", "刹车",
};

constexpr std::array<std::string_view, 3> kOpenCameraPhrases{"open camera", "打开相机",
                                                             "打开摄像头"};
constexpr std::array<std::string_view, 3> kPlayMusicPhrases{"play music", "播放音乐", "放音乐"};
constexpr std::array<std::string_view, 6> kVehicleStatusPhrases{
    "vehicle status", "battery level", "车辆状态", "查看车辆状态", "查看电量", "电量多少"};

}  // namespace

DeterministicCommandRoute DeterministicCommandRouter::Route(
    std::string_view normalized_text) const {
  if (normalized_text.empty() || ContainsAny(normalized_text, kNegations) ||
      ContainsAny(normalized_text, kUnsupportedVehicleParameters)) {
    return {};
  }

  const bool open_camera = ContainsAny(normalized_text, kOpenCameraPhrases);
  const bool play_music = ContainsAny(normalized_text, kPlayMusicPhrases);
  const bool vehicle_status = ContainsAny(normalized_text, kVehicleStatusPhrases);
  const int matches = static_cast<int>(open_camera) + static_cast<int>(play_music) +
                      static_cast<int>(vehicle_status);
  if (matches != 1) {
    return {};
  }
  if (open_camera) {
    return {VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera};
  }
  if (play_music) {
    return {VoiceIntent::kPlayMusic, VoiceAction::kPlayMusic};
  }
  return {VoiceIntent::kShowVehicleStatus, VoiceAction::kQueryVehicleStatus};
}

}  // namespace voice
}  // namespace cockpit
