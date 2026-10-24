#pragma once

#include <cstdlib>
#include <filesystem>

namespace cockpit {
namespace agent {
namespace sherpa {

inline std::filesystem::path ResolveAiRoot() {
  const char* root = std::getenv("COCKPIT_AI_ROOT");
  if (root != nullptr && root[0] != '\0') {
    return std::filesystem::path(root);
  }
  return std::filesystem::path("_output") / "ai";
}

}  // namespace sherpa
}  // namespace agent
}  // namespace cockpit
