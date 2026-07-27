#include "cockpit/library/upgrader/upgrader_runtime.h"

#include <exception>
#include <filesystem>
#include <string>

#include "cockpit/core/config/system_config.h"
#include "cockpit/core/logging/logger.h"
#include "cockpit/library/upgrader/upgrade_transaction.h"

namespace cockpit {
namespace upgrader {

UpgraderRuntime::~UpgraderRuntime() {
  Stop();
}

bool UpgraderRuntime::Start(const std::string& config_path) {
  if (active_.load()) {
    return false;
  }
  try {
    const auto config = config::SystemConfig::LoadFromFile(config_path);
    logging::InitLogger("upgrader", config.paths().log_dir,
                        logging::ParseLevel(config.logging().level), config.logging().mirror_stderr,
                        config.logging().dump_time_secs, config.logging().cut_off_time_mins,
                        config.logging().max_files);

    const std::filesystem::path install_root =
        std::filesystem::absolute(config_path).parent_path().parent_path();
    request_path_ = install_root / "run/upgrade-request.yaml";
    result_path_ = install_root / "run/upgrade-result.yaml";

    UpgradeRequest request;
    std::string error;
    if (!LoadUpgradeRequest(request_path_, &request, &error)) {
      LOG_ERROR(error);
      return false;
    }
    if (std::filesystem::weakly_canonical(request.install_root) !=
        std::filesystem::weakly_canonical(install_root)) {
      LOG_ERROR("upgrade request install root does not match runtime root");
      return false;
    }

    std::error_code filesystem_error;
    std::filesystem::remove(result_path_, filesystem_error);
    active_.store(true);
    result_.store(0);
    worker_ = std::thread([this, request] {
      UpgradeResult upgrade_result;
      InstallUpgrade(request, &upgrade_result, &active_);
      std::string error;
      if (!SaveUpgradeResult(result_path_, upgrade_result, &error)) {
        LOG_ERROR(error);
        result_.store(1);
        return;
      }
      if (upgrade_result.state == UpgradeState::kActivated) {
        LOG_INFO("upgrade release activated version=" + upgrade_result.version);
      } else {
        LOG_ERROR("upgrade failed: " + upgrade_result.error);
      }
    });
    return true;
  } catch (const std::exception& exception) {
    LOG_ERROR("failed to start upgrader: " + std::string(exception.what()));
    active_.store(false);
    return false;
  }
}

void UpgraderRuntime::Stop() {
  if (!active_.exchange(false)) {
    return;
  }
  if (worker_.joinable()) {
    worker_.join();
  }
}

int UpgraderRuntime::Poll() const {
  return active_.load() ? result_.load() : 1;
}

}  // namespace upgrader
}  // namespace cockpit
