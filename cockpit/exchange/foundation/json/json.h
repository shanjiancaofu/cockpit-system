#pragma once

#include <string>
#include <string_view>

namespace cockpit {
namespace json {

std::string EscapeString(std::string_view input);
bool IsValidValue(std::string_view input, std::string* error = nullptr);

}  // namespace json
}  // namespace cockpit
