#pragma once

#include <string>

namespace cockpit {
namespace build {

struct BuildInfo {
  std::string version;
  std::string build_type;
  std::string git_commit;
  bool git_dirty = false;
};

BuildInfo GetBuildInfo();

}  // namespace build
}  // namespace cockpit
