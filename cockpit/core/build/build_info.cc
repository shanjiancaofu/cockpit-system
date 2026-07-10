#include "cockpit/core/build/build_info.h"

#include "build_info_config.h"

namespace cockpit {
namespace build {

BuildInfo GetBuildInfo() {
  BuildInfo info;
  info.version = COCKPIT_PROJECT_VERSION;
  info.build_type = COCKPIT_BUILD_TYPE;
  info.git_commit = COCKPIT_GIT_COMMIT;
  info.git_dirty = std::string(COCKPIT_GIT_DIRTY) == "true";
  return info;
}

}  // namespace build
}  // namespace cockpit
