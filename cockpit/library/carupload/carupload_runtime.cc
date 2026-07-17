#include "cockpit/library/carupload/carupload_runtime.h"

#include <exception>
#include <string>

#include "cockpit/core/config/system_config.h"
#include "cockpit/core/logging/logger.h"

namespace cockpit {
namespace carupload {

bool CaruploadRuntime::Start(const std::string& config_path) {
  if (running_.load()) {
    return false;
  }
  try {
    const auto config = config::SystemConfig::LoadFromFile(config_path);
    logging::InitLogger("carupload", config.paths().log_dir,
                        logging::ParseLevel(config.logging().level), config.logging().mirror_stderr,
                        config.logging().dump_time_secs, config.logging().cut_off_time_mins,
                        config.logging().max_files);
    LOG_WARN("carupload transport is not implemented; cloud mode validates module lifecycle only");
  } catch (const std::exception& error) {
    LOG_ERROR("failed to configure carupload: " + std::string(error.what()));
    return false;
  }
  running_.store(true);
  return true;
}

void CaruploadRuntime::Stop() {
  running_.store(false);
}

int CaruploadRuntime::Poll() const {
  return running_.load() ? 0 : 1;
}

}  // namespace carupload
}  // namespace cockpit
