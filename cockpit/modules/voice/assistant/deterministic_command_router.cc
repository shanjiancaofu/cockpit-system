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

bool IsEnglishPhrase(std::string_view phrase) {
  return static_cast<unsigned char>(phrase.front()) < 0x80U;
}

bool ContainsPhrase(std::string_view text, std::string_view phrase) {
  return IsEnglishPhrase(phrase) ? ContainsEnglishPhrase(text, phrase)
                                 : text.find(phrase) != std::string_view::npos;
}

bool EndsWith(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

bool IsNegatedOccurrence(std::string_view text, std::size_t phrase_position, bool english_phrase) {
  std::string_view context = text.substr(0, phrase_position);
  while (!context.empty() && context.back() == ' ') {
    context.remove_suffix(1);
  }
  if (english_phrase) {
    constexpr std::size_t kEnglishContextBytes = 32;
    if (context.size() > kEnglishContextBytes) {
      context.remove_prefix(context.size() - kEnglishContextBytes);
    }
    constexpr std::array<std::string_view, 3> kEnglishNegations{"don't", "do not", "never"};
    for (const std::string_view negation : kEnglishNegations) {
      if (ContainsEnglishPhrase(context, negation)) {
        return true;
      }
    }
    return false;
  }

  constexpr std::size_t kChineseContextBytes = 24;
  if (context.size() > kChineseContextBytes) {
    context.remove_prefix(context.size() - kChineseContextBytes);
  }
  constexpr std::array<std::string_view, 4> kChineseStrongNegations{"不需要", "请勿", "不用",
                                                                    "不要"};
  for (const std::string_view negation : kChineseStrongNegations) {
    if (context.find(negation) != std::string_view::npos) {
      return true;
    }
  }
  constexpr std::array<std::string_view, 2> kChineseDirectNegations{"别", "不"};
  for (const std::string_view negation : kChineseDirectNegations) {
    if (EndsWith(context, negation)) {
      return true;
    }
  }
  return false;
}

struct ActionMatch {
  bool positive{false};
  bool negated{false};
};

template <std::size_t Size>
ActionMatch MatchAction(std::string_view text, const std::array<std::string_view, Size>& phrases) {
  ActionMatch match;
  for (const std::string_view phrase : phrases) {
    const bool english_phrase = IsEnglishPhrase(phrase);
    std::size_t position = text.find(phrase);
    while (position != std::string_view::npos) {
      const std::size_t end = position + phrase.size();
      const bool valid_occurrence =
          !english_phrase || ((position == 0 || !IsAsciiWordCharacter(text[position - 1])) &&
                              (end == text.size() || !IsAsciiWordCharacter(text[end])));
      if (valid_occurrence) {
        if (IsNegatedOccurrence(text, position, english_phrase)) {
          match.negated = true;
        } else {
          match.positive = true;
        }
      }
      position = text.find(phrase, position + 1);
    }
  }
  return match;
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
  if (normalized_text.empty() || ContainsAny(normalized_text, kUnsupportedVehicleParameters)) {
    return {};
  }

  const ActionMatch open_camera = MatchAction(normalized_text, kOpenCameraPhrases);
  const ActionMatch play_music = MatchAction(normalized_text, kPlayMusicPhrases);
  const ActionMatch vehicle_status = MatchAction(normalized_text, kVehicleStatusPhrases);
  if (open_camera.negated || play_music.negated || vehicle_status.negated) {
    return {};
  }
  const int matches = static_cast<int>(open_camera.positive) +
                      static_cast<int>(play_music.positive) +
                      static_cast<int>(vehicle_status.positive);
  if (matches != 1) {
    return {};
  }
  if (open_camera.positive) {
    return {VoiceIntent::kOpenCamera, VoiceAction::kOpenCamera};
  }
  if (play_music.positive) {
    return {VoiceIntent::kPlayMusic, VoiceAction::kPlayMusic};
  }
  return {VoiceIntent::kShowVehicleStatus, VoiceAction::kQueryVehicleStatus};
}

}  // namespace voice
}  // namespace cockpit
