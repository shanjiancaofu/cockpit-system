#include "cockpit/core/json/json.h"

#include <cctype>
#include <cstddef>
#include <cstdint>

namespace cockpit {
namespace json {
namespace {

constexpr std::size_t kMaximumDepth = 128;

bool IsHexDigit(char character) {
  return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
         (character >= 'A' && character <= 'F');
}

bool IsContinuationByte(unsigned char character) {
  return character >= 0x80U && character <= 0xBFU;
}

std::size_t Utf8SequenceLength(std::string_view input, std::size_t offset) {
  if (offset >= input.size()) {
    return 0;
  }
  const unsigned char first = static_cast<unsigned char>(input[offset]);
  if (first <= 0x7FU) {
    return 1;
  }
  if (first >= 0xC2U && first <= 0xDFU) {
    return offset + 1U < input.size() &&
                   IsContinuationByte(static_cast<unsigned char>(input[offset + 1U]))
               ? 2U
               : 0U;
  }
  if (first >= 0xE0U && first <= 0xEFU) {
    if (offset + 2U >= input.size()) {
      return 0;
    }
    const unsigned char second = static_cast<unsigned char>(input[offset + 1U]);
    const unsigned char third = static_cast<unsigned char>(input[offset + 2U]);
    if (!IsContinuationByte(third) || !IsContinuationByte(second) ||
        (first == 0xE0U && second < 0xA0U) || (first == 0xEDU && second > 0x9FU)) {
      return 0;
    }
    return 3U;
  }
  if (first >= 0xF0U && first <= 0xF4U) {
    if (offset + 3U >= input.size()) {
      return 0;
    }
    const unsigned char second = static_cast<unsigned char>(input[offset + 1U]);
    const unsigned char third = static_cast<unsigned char>(input[offset + 2U]);
    const unsigned char fourth = static_cast<unsigned char>(input[offset + 3U]);
    if (!IsContinuationByte(second) || !IsContinuationByte(third) || !IsContinuationByte(fourth) ||
        (first == 0xF0U && second < 0x90U) || (first == 0xF4U && second > 0x8FU)) {
      return 0;
    }
    return 4U;
  }
  return 0;
}

class Parser {
 public:
  explicit Parser(std::string_view input) : input_(input) {
  }

  bool Parse(std::string* error) {
    SkipWhitespace();
    if (!ParseValue(0)) {
      AssignError(error);
      return false;
    }
    SkipWhitespace();
    if (position_ != input_.size()) {
      Fail("unexpected trailing characters");
      AssignError(error);
      return false;
    }
    return true;
  }

 private:
  bool ParseValue(std::size_t depth) {  // NOLINT(misc-no-recursion)
    if (depth > kMaximumDepth) {
      return Fail("JSON nesting is too deep");
    }
    SkipWhitespace();
    if (position_ == input_.size()) {
      return Fail("expected JSON value");
    }
    switch (input_[position_]) {
      case '{':
        return ParseObject(depth + 1U);
      case '[':
        return ParseArray(depth + 1U);
      case '"':
        return ParseString();
      case 't':
        return ParseLiteral("true");
      case 'f':
        return ParseLiteral("false");
      case 'n':
        return ParseLiteral("null");
      default:
        return ParseNumber();
    }
  }

  bool ParseObject(std::size_t depth) {  // NOLINT(misc-no-recursion)
    ++position_;
    SkipWhitespace();
    if (Consume('}')) {
      return true;
    }
    while (true) {
      if (!ParseString()) {
        return false;
      }
      SkipWhitespace();
      if (!Consume(':')) {
        return Fail("expected ':' after JSON object key");
      }
      if (!ParseValue(depth)) {
        return false;
      }
      SkipWhitespace();
      if (Consume('}')) {
        return true;
      }
      if (!Consume(',')) {
        return Fail("expected ',' or '}' in JSON object");
      }
      SkipWhitespace();
    }
  }

  bool ParseArray(std::size_t depth) {  // NOLINT(misc-no-recursion)
    ++position_;
    SkipWhitespace();
    if (Consume(']')) {
      return true;
    }
    while (true) {
      if (!ParseValue(depth)) {
        return false;
      }
      SkipWhitespace();
      if (Consume(']')) {
        return true;
      }
      if (!Consume(',')) {
        return Fail("expected ',' or ']' in JSON array");
      }
      SkipWhitespace();
    }
  }

  bool ParseString() {
    if (!Consume('"')) {
      return Fail("expected JSON string");
    }
    while (position_ < input_.size()) {
      const unsigned char character = static_cast<unsigned char>(input_[position_]);
      if (character == '"') {
        ++position_;
        return true;
      }
      if (character < 0x20U) {
        return Fail("unescaped control character in JSON string");
      }
      if (character == '\\') {
        ++position_;
        if (position_ == input_.size()) {
          return Fail("incomplete JSON escape sequence");
        }
        const char escape = input_[position_++];
        switch (escape) {
          case '"':
          case '\\':
          case '/':
          case 'b':
          case 'f':
          case 'n':
          case 'r':
          case 't':
            continue;
          case 'u':
            if (position_ + 4U > input_.size()) {
              return Fail("incomplete JSON unicode escape");
            }
            for (std::size_t index = 0; index < 4U; ++index) {
              if (!IsHexDigit(input_[position_ + index])) {
                return Fail("invalid JSON unicode escape");
              }
            }
            position_ += 4U;
            continue;
          default:
            return Fail("invalid JSON escape sequence");
        }
      }
      if (character >= 0x80U) {
        const std::size_t length = Utf8SequenceLength(input_, position_);
        if (length == 0U) {
          return Fail("invalid UTF-8 in JSON string");
        }
        position_ += length;
      } else {
        ++position_;
      }
    }
    return Fail("unterminated JSON string");
  }

  bool ParseLiteral(std::string_view literal) {
    if (input_.substr(position_, literal.size()) != literal) {
      return Fail("invalid JSON literal");
    }
    position_ += literal.size();
    return true;
  }

  bool ParseNumber() {
    const std::size_t start = position_;
    Consume('-');
    if (position_ == input_.size()) {
      return Fail("invalid JSON number");
    }
    if (Consume('0')) {
      if (position_ < input_.size() &&
          std::isdigit(static_cast<unsigned char>(input_[position_]))) {
        return Fail("invalid leading zero in JSON number");
      }
    } else {
      if (position_ == input_.size() ||
          !std::isdigit(static_cast<unsigned char>(input_[position_]))) {
        return Fail("invalid JSON number");
      }
      do {
        ++position_;
      } while (position_ < input_.size() &&
               std::isdigit(static_cast<unsigned char>(input_[position_])));
    }
    if (Consume('.')) {
      if (position_ == input_.size() ||
          !std::isdigit(static_cast<unsigned char>(input_[position_]))) {
        return Fail("invalid JSON fraction");
      }
      do {
        ++position_;
      } while (position_ < input_.size() &&
               std::isdigit(static_cast<unsigned char>(input_[position_])));
    }
    if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
      ++position_;
      if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) {
        ++position_;
      }
      if (position_ == input_.size() ||
          !std::isdigit(static_cast<unsigned char>(input_[position_]))) {
        return Fail("invalid JSON exponent");
      }
      do {
        ++position_;
      } while (position_ < input_.size() &&
               std::isdigit(static_cast<unsigned char>(input_[position_])));
    }
    return position_ > start;
  }

  bool Consume(char character) {
    if (position_ >= input_.size() || input_[position_] != character) {
      return false;
    }
    ++position_;
    return true;
  }

  void SkipWhitespace() {
    while (position_ < input_.size() && (input_[position_] == ' ' || input_[position_] == '\n' ||
                                         input_[position_] == '\r' || input_[position_] == '\t')) {
      ++position_;
    }
  }

  bool Fail(const char* message) {
    if (error_.empty()) {
      error_ = message;
    }
    return false;
  }

  void AssignError(std::string* error) const {
    if (error != nullptr) {
      *error = error_.empty() ? "invalid JSON value" : error_;
    }
  }

  const std::string_view input_;
  std::size_t position_ = 0;
  std::string error_;
};

char HexDigit(unsigned char value) {
  return value < 10U ? static_cast<char>('0' + value) : static_cast<char>('A' + value - 10U);
}

}  // namespace

std::string EscapeString(std::string_view input) {
  std::string output;
  output.reserve(input.size());
  for (std::size_t index = 0; index < input.size();) {
    const unsigned char character = static_cast<unsigned char>(input[index]);
    switch (character) {
      case '\\':
        output += "\\\\";
        ++index;
        break;
      case '"':
        output += "\\\"";
        ++index;
        break;
      case '\b':
        output += "\\b";
        ++index;
        break;
      case '\f':
        output += "\\f";
        ++index;
        break;
      case '\n':
        output += "\\n";
        ++index;
        break;
      case '\r':
        output += "\\r";
        ++index;
        break;
      case '\t':
        output += "\\t";
        ++index;
        break;
      default:
        if (character < 0x20U) {
          output += "\\u00";
          output.push_back(HexDigit(static_cast<unsigned char>(character >> 4U)));
          output.push_back(HexDigit(static_cast<unsigned char>(character & 0x0FU)));
          ++index;
          break;
        }
        if (character >= 0x80U) {
          const std::size_t length = Utf8SequenceLength(input, index);
          if (length == 0U) {
            output += "\\u00";
            output.push_back(HexDigit(static_cast<unsigned char>(character >> 4U)));
            output.push_back(HexDigit(static_cast<unsigned char>(character & 0x0FU)));
            ++index;
            break;
          }
          output.append(input.data() + index, length);
          index += length;
          break;
        }
        output.push_back(static_cast<char>(character));
        ++index;
        break;
    }
  }
  return output;
}

bool IsValidValue(std::string_view input, std::string* error) {
  return Parser(input).Parse(error);
}

}  // namespace json
}  // namespace cockpit
