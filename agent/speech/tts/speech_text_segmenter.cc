#include "agent/speech/tts/speech_text_segmenter.h"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <vector>

namespace cockpit {
namespace voice {
namespace {

struct TextToken {
  std::size_t begin = 0U;
  std::size_t end = 0U;
  bool hard_boundary = false;
  bool soft_boundary = false;
};

std::size_t Utf8CodePointBytes(unsigned char lead) {
  if ((lead & 0x80U) == 0U) return 1U;
  if ((lead & 0xE0U) == 0xC0U) return 2U;
  if ((lead & 0xF0U) == 0xE0U) return 3U;
  if ((lead & 0xF8U) == 0xF0U) return 4U;
  return 1U;
}

bool IsHardBoundary(std::string_view token) {
  return token == "." || token == "?" || token == "!" || token == ";" || token == "\n" ||
         token == "。" || token == "？" || token == "！" || token == "；";
}

bool IsSoftBoundary(std::string_view token) {
  return token == " " || token == "\t" || token == "," || token == ":" || token == "，" ||
         token == "：" || token == "、";
}

std::string TrimAsciiWhitespace(std::string_view input) {
  while (!input.empty() && std::isspace(static_cast<unsigned char>(input.front()))) {
    input.remove_prefix(1U);
  }
  while (!input.empty() && std::isspace(static_cast<unsigned char>(input.back()))) {
    input.remove_suffix(1U);
  }
  return std::string(input);
}

}  // namespace

std::vector<std::string> SplitSpeechText(std::string_view text, std::size_t max_codepoints) {
  if (text.empty() || max_codepoints == 0U) {
    return {};
  }

  std::vector<TextToken> tokens;
  for (std::size_t offset = 0U; offset < text.size();) {
    const std::size_t length = std::min(
        Utf8CodePointBytes(static_cast<unsigned char>(text[offset])), text.size() - offset);
    const std::string_view token = text.substr(offset, length);
    tokens.push_back({offset, offset + length, IsHardBoundary(token), IsSoftBoundary(token)});
    offset += length;
  }

  std::vector<std::string> segments;
  std::size_t segment_start = 0U;
  std::size_t index = 0U;
  std::size_t last_soft_boundary = tokens.size();
  while (index < tokens.size()) {
    if (tokens[index].soft_boundary) {
      last_soft_boundary = index;
    }
    const std::size_t segment_length = index - segment_start + 1U;
    std::size_t segment_end = tokens.size();
    if (tokens[index].hard_boundary) {
      segment_end = index + 1U;
    } else if (segment_length >= max_codepoints) {
      segment_end = last_soft_boundary != tokens.size() && last_soft_boundary >= segment_start
                        ? last_soft_boundary + 1U
                        : index + 1U;
    }

    if (segment_end != tokens.size()) {
      const std::size_t byte_begin = tokens[segment_start].begin;
      const std::size_t byte_end = tokens[segment_end - 1U].end;
      std::string segment = TrimAsciiWhitespace(text.substr(byte_begin, byte_end - byte_begin));
      if (!segment.empty()) {
        segments.push_back(std::move(segment));
      }
      segment_start = segment_end;
      index = segment_start;
      last_soft_boundary = tokens.size();
      continue;
    }
    ++index;
  }

  if (segment_start < tokens.size()) {
    const std::size_t byte_begin = tokens[segment_start].begin;
    std::string segment = TrimAsciiWhitespace(text.substr(byte_begin));
    if (!segment.empty()) {
      segments.push_back(std::move(segment));
    }
  }
  return segments;
}

}  // namespace voice
}  // namespace cockpit
