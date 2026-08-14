#include "cockpit/modules/voice/assistant/transcript_normalizer.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace cockpit {
namespace voice {
namespace {

struct Utf8CodePoint {
  std::uint32_t value{0};
  std::size_t size{1};
  bool valid{false};
};

bool IsContinuation(unsigned char value) {
  return (value & 0xC0U) == 0x80U;
}

Utf8CodePoint Decode(std::string_view text, std::size_t offset) {
  const auto first = static_cast<unsigned char>(text[offset]);
  if (first < 0x80U) {
    return {first, 1, true};
  }

  std::size_t size = 0;
  std::uint32_t value = 0;
  std::uint32_t minimum = 0;
  if ((first & 0xE0U) == 0xC0U) {
    size = 2;
    value = first & 0x1FU;
    minimum = 0x80U;
  } else if ((first & 0xF0U) == 0xE0U) {
    size = 3;
    value = first & 0x0FU;
    minimum = 0x800U;
  } else if ((first & 0xF8U) == 0xF0U) {
    size = 4;
    value = first & 0x07U;
    minimum = 0x10000U;
  } else {
    return {first, 1, false};
  }
  if (offset + size > text.size()) {
    return {first, 1, false};
  }
  for (std::size_t index = 1; index < size; ++index) {
    const auto next = static_cast<unsigned char>(text[offset + index]);
    if (!IsContinuation(next)) {
      return {first, 1, false};
    }
    value = (value << 6U) | (next & 0x3FU);
  }
  if (value < minimum || value > 0x10FFFFU || (value >= 0xD800U && value <= 0xDFFFU)) {
    return {first, 1, false};
  }
  return {value, size, true};
}

bool IsSeparator(std::uint32_t value) {
  if (value < 0x80U) {
    const auto ascii = static_cast<unsigned char>(value);
    return std::isspace(ascii) != 0 || value == ',' || value == '.' || value == '!' ||
           value == '?' || value == ';' || value == ':';
  }
  return value == 0x3000U || value == 0x3001U || value == 0x3002U || value == 0xFF0CU ||
         value == 0xFF1BU || value == 0xFF1AU;
}

void AppendCodePoint(std::uint32_t value, std::string* output) {
  if (value <= 0x7FU) {
    output->push_back(static_cast<char>(value));
  } else if (value <= 0x7FFU) {
    output->push_back(static_cast<char>(0xC0U | (value >> 6U)));
    output->push_back(static_cast<char>(0x80U | (value & 0x3FU)));
  } else if (value <= 0xFFFFU) {
    output->push_back(static_cast<char>(0xE0U | (value >> 12U)));
    output->push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
    output->push_back(static_cast<char>(0x80U | (value & 0x3FU)));
  } else {
    output->push_back(static_cast<char>(0xF0U | (value >> 18U)));
    output->push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3FU)));
    output->push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
    output->push_back(static_cast<char>(0x80U | (value & 0x3FU)));
  }
}

}  // namespace

std::string TranscriptNormalizer::Normalize(std::string_view transcript) {
  std::string normalized;
  normalized.reserve(transcript.size());
  bool pending_space = false;
  for (std::size_t offset = 0; offset < transcript.size();) {
    const Utf8CodePoint decoded = Decode(transcript, offset);
    offset += decoded.size;
    if (!decoded.valid) {
      if (pending_space && !normalized.empty()) {
        normalized.push_back(' ');
      }
      pending_space = false;
      normalized.push_back(static_cast<char>(decoded.value));
      continue;
    }

    std::uint32_t value = decoded.value;
    if (value >= 0xFF01U && value <= 0xFF5EU) {
      value -= 0xFEE0U;
    }
    if (IsSeparator(value)) {
      pending_space = !normalized.empty();
      continue;
    }
    if (pending_space && !normalized.empty()) {
      normalized.push_back(' ');
    }
    pending_space = false;
    if (value >= 'A' && value <= 'Z') {
      value += 'a' - 'A';
    }
    AppendCodePoint(value, &normalized);
  }
  return normalized;
}

}  // namespace voice
}  // namespace cockpit
