#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace cockpit {
namespace voice {

std::vector<std::string> SplitSpeechText(std::string_view text,
                                         std::size_t max_codepoints = 80U);

}  // namespace voice
}  // namespace cockpit
