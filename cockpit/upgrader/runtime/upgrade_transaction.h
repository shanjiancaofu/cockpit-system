#pragma once

#include <atomic>
#include <filesystem>
#include <string>

namespace cockpit {
namespace upgrader {

struct UpgradeRequest {
  std::filesystem::path package_root;
  std::filesystem::path install_root;
  std::filesystem::path public_key;
  std::string version;
};

enum class UpgradeState {
  kActivated,
  kFailed,
};

struct UpgradeResult {
  UpgradeState state{UpgradeState::kFailed};
  std::string version;
  std::filesystem::path previous_release;
  std::string error;
};

bool ReadUpgradePackageVersion(const std::filesystem::path& package_root, std::string* version,
                               std::string* error);
bool InstallUpgrade(const UpgradeRequest& request, UpgradeResult* result,
                    const std::atomic_bool* running = nullptr);
bool RecoverInterruptedUpgrade(const std::filesystem::path& install_root, bool* recovered,
                               std::string* error);
bool ConfirmUpgrade(const std::filesystem::path& install_root, const std::string& version,
                    std::string* error);
bool RollbackUpgrade(const std::filesystem::path& install_root,
                     const std::filesystem::path& previous_release, std::string* error);
bool WaitForUpgradeHealth(const std::filesystem::path& command,
                          const std::filesystem::path& install_root, int timeout_seconds,
                          std::string* error);

bool SaveUpgradeRequest(const std::filesystem::path& path, const UpgradeRequest& request,
                        std::string* error);
bool LoadUpgradeRequest(const std::filesystem::path& path, UpgradeRequest* request,
                        std::string* error);
bool SaveUpgradeResult(const std::filesystem::path& path, const UpgradeResult& result,
                       std::string* error);
bool LoadUpgradeResult(const std::filesystem::path& path, UpgradeResult* result,
                       std::string* error);

}  // namespace upgrader
}  // namespace cockpit
