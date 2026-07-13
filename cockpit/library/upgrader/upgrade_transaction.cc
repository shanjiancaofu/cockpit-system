#include "cockpit/library/upgrader/upgrade_transaction.h"

#include <sys/wait.h>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace cockpit {
namespace upgrader {
namespace {

bool Fail(std::string* error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
  return false;
}

bool IsValidVersion(const std::string& version) {
  if (version.empty()) {
    return false;
  }
  return std::all_of(version.begin(), version.end(), [](unsigned char value) {
    return std::isalnum(value) != 0 || value == '.' || value == '_' || value == '-';
  });
}

bool IsSafeReleaseTarget(const std::filesystem::path& path) {
  auto part = path.begin();
  if (path.is_absolute() || path.lexically_normal() != path || part == path.end() ||
      part->string() != "releases") {
    return false;
  }
  ++part;
  if (part == path.end() || !IsValidVersion(part->string())) {
    return false;
  }
  ++part;
  return part == path.end();
}

bool IsSafeManifestPath(const std::string& value) {
  const std::filesystem::path path(value);
  if (value.empty() || path.is_absolute() || path.lexically_normal() != path) {
    return false;
  }
  const auto first = path.begin();
  if (first == path.end()) {
    return false;
  }
  const std::string directory = first->string();
  return directory == "release" || directory == "config" || directory == "systemd" ||
         directory == "deploy" || directory == "manifest";
}

bool ValidateChecksumManifest(const std::filesystem::path& package_root, std::string* error) {
  std::ifstream input(package_root / "manifest/SHA256SUMS");
  if (!input.is_open()) {
    return Fail(error, "missing manifest/SHA256SUMS");
  }

  bool has_version = false;
  bool has_installer = false;
  std::size_t entries = 0;
  std::string line;
  while (std::getline(input, line)) {
    if (line.size() < 67 || line[64] != ' ' || (line[65] != ' ' && line[65] != '*') ||
        !std::all_of(line.begin(), line.begin() + 64, [](unsigned char value) {
          return std::isxdigit(value) != 0;
        })) {
      return Fail(error, "invalid SHA256SUMS line");
    }
    const std::string path = line.substr(66);
    if (!IsSafeManifestPath(path)) {
      return Fail(error, "unsafe path in SHA256SUMS: " + path);
    }
    has_version |= path == "manifest/VERSION";
    has_installer |= path == "deploy/install.sh";
    ++entries;
  }
  if (input.bad()) {
    return Fail(error, "failed to read manifest/SHA256SUMS");
  }
  if (entries == 0 || !has_version || !has_installer) {
    return Fail(error, "SHA256SUMS must include manifest/VERSION and deploy/install.sh");
  }
  return true;
}

int RunProcess(const std::vector<std::string>& arguments, const std::filesystem::path& directory,
               const std::vector<std::pair<std::string, std::string>>& environment) {
  std::vector<char*> argv;
  argv.reserve(arguments.size() + 1);
  for (const std::string& argument : arguments) {
    argv.push_back(const_cast<char*>(argument.c_str()));
  }
  argv.push_back(nullptr);

  const pid_t pid = fork();
  if (pid < 0) {
    return -1;
  }
  if (pid == 0) {
    if (!directory.empty() && chdir(directory.c_str()) != 0) {
      _exit(126);
    }
    for (const auto& value : environment) {
      if (setenv(value.first.c_str(), value.second.c_str(), 1) != 0) {
        _exit(126);
      }
    }
    execv(arguments.front().c_str(), argv.data());
    _exit(127);
  }

  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) {
      return -1;
    }
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

bool WriteYaml(const std::filesystem::path& path, const YAML::Emitter& output, std::string* error) {
  std::error_code filesystem_error;
  std::filesystem::create_directories(path.parent_path(), filesystem_error);
  if (filesystem_error) {
    return Fail(error, "create upgrade state directory failed: " + filesystem_error.message());
  }
  const std::filesystem::path temporary = path.string() + ".tmp";
  {
    std::ofstream file(temporary, std::ios::trunc);
    if (!file.is_open()) {
      return Fail(error, "open temporary upgrade state failed: " + temporary.string());
    }
    file << output.c_str() << '\n';
    if (!file.good()) {
      return Fail(error, "write temporary upgrade state failed: " + temporary.string());
    }
  }
  std::filesystem::rename(temporary, path, filesystem_error);
  if (filesystem_error) {
    std::filesystem::remove(temporary);
    return Fail(error, "publish upgrade state failed: " + filesystem_error.message());
  }
  return true;
}

bool ReadRequiredScalar(const YAML::Node& root, const char* key, std::string* value,
                        std::string* error) {
  const YAML::Node node = root[key];
  if (!node || !node.IsScalar()) {
    return Fail(error, std::string("upgrade state requires scalar ") + key);
  }
  *value = node.as<std::string>();
  return true;
}

enum class TransactionState {
  kPrepared,
  kActivated,
  kConfirmed,
};

struct UpgradeTransaction {
  TransactionState state{TransactionState::kPrepared};
  std::string version;
  std::filesystem::path previous_release;
};

bool SaveTransaction(const std::filesystem::path& path, const UpgradeTransaction& transaction,
                     std::string* error) {
  const char* state = "prepared";
  if (transaction.state == TransactionState::kActivated) {
    state = "activated";
  } else if (transaction.state == TransactionState::kConfirmed) {
    state = "confirmed";
  }
  YAML::Emitter output;
  output << YAML::BeginMap << YAML::Key << "state" << YAML::Value << state << YAML::Key << "version"
         << YAML::Value << transaction.version << YAML::Key << "previous_release" << YAML::Value
         << transaction.previous_release.string() << YAML::EndMap;
  return output.good() ? WriteYaml(path, output, error)
                       : Fail(error, "serialize upgrade transaction failed");
}

bool LoadTransaction(const std::filesystem::path& path, UpgradeTransaction* transaction,
                     std::string* error) {
  try {
    const YAML::Node root = YAML::LoadFile(path.string());
    std::string state;
    std::string previous_release;
    if (!root.IsMap() || !ReadRequiredScalar(root, "state", &state, error) ||
        !ReadRequiredScalar(root, "version", &transaction->version, error) ||
        !ReadRequiredScalar(root, "previous_release", &previous_release, error)) {
      return false;
    }
    if (!IsValidVersion(transaction->version)) {
      return Fail(error, "upgrade transaction version is invalid");
    }
    transaction->previous_release = previous_release;
    if (!transaction->previous_release.empty() &&
        !IsSafeReleaseTarget(transaction->previous_release)) {
      return Fail(error, "upgrade transaction previous release is unsafe");
    }
    if (transaction->previous_release == std::filesystem::path("releases") / transaction->version) {
      return Fail(error, "upgrade transaction candidate matches previous release");
    }
    if (state == "prepared") {
      transaction->state = TransactionState::kPrepared;
    } else if (state == "activated") {
      transaction->state = TransactionState::kActivated;
    } else if (state == "confirmed") {
      transaction->state = TransactionState::kConfirmed;
    } else {
      return Fail(error, "upgrade transaction state is invalid");
    }
    return true;
  } catch (const std::exception& exception) {
    return Fail(error, "load upgrade transaction failed: " + std::string(exception.what()));
  }
}

}  // namespace

bool ReadUpgradePackageVersion(const std::filesystem::path& package_root, std::string* version,
                               std::string* error) {
  if (version == nullptr) {
    return Fail(error, "upgrade version result must not be null");
  }
  try {
    const std::filesystem::path root = std::filesystem::canonical(package_root);
    std::ifstream input(root / "manifest/VERSION");
    std::string package_version;
    std::getline(input, package_version);
    std::string trailing;
    if (!input.is_open() || !IsValidVersion(package_version) || std::getline(input, trailing)) {
      return Fail(error, "manifest/VERSION must contain one valid version");
    }
    *version = std::move(package_version);
    return true;
  } catch (const std::exception& exception) {
    return Fail(error, "read upgrade package version failed: " + std::string(exception.what()));
  }
}

bool InstallUpgrade(const UpgradeRequest& request, UpgradeResult* result) {
  if (result == nullptr) {
    return false;
  }
  *result = UpgradeResult{};
  result->version = request.version;

  try {
    const std::filesystem::path package_root = std::filesystem::canonical(request.package_root);
    if (!std::filesystem::is_directory(package_root / "release")) {
      result->error = "upgrade package is missing release directory";
      return false;
    }
    if (!std::filesystem::is_regular_file(package_root / "deploy/install.sh")) {
      result->error = "upgrade package is missing deploy/install.sh";
      return false;
    }
    if (!ValidateChecksumManifest(package_root, &result->error)) {
      return false;
    }
    std::string package_version;
    if (!ReadUpgradePackageVersion(package_root, &package_version, &result->error)) {
      return false;
    }
    if (request.version != package_version) {
      result->error = "confirmed version does not match package: " + package_version;
      return false;
    }
    if (RunProcess({"/usr/bin/sha256sum", "--check", "--quiet", "--strict", "manifest/SHA256SUMS"},
                   package_root, {}) != 0) {
      result->error = "package checksum verification failed";
      return false;
    }

    const std::filesystem::path install_root = std::filesystem::absolute(request.install_root);
    const std::filesystem::path current = install_root / "current";
    const std::filesystem::path transaction_path = install_root / "run/upgrade-transaction.yaml";
    const std::filesystem::file_status current_status = std::filesystem::symlink_status(current);
    if (std::filesystem::exists(current_status)) {
      if (!std::filesystem::is_symlink(current_status)) {
        result->error = "install current path is not a symlink";
        return false;
      }
      result->previous_release = std::filesystem::read_symlink(current);
      if (!IsSafeReleaseTarget(result->previous_release) ||
          !std::filesystem::is_directory(install_root / result->previous_release)) {
        result->error = "install current symlink target is invalid";
        return false;
      }
    }
    if (std::filesystem::exists(
            std::filesystem::symlink_status(install_root / "releases" / request.version))) {
      result->error = "release already exists: " + request.version;
      return false;
    }

    UpgradeTransaction transaction;
    transaction.version = request.version;
    transaction.previous_release = result->previous_release;
    if (!SaveTransaction(transaction_path, transaction, &result->error)) {
      return false;
    }

    const std::filesystem::path installer = package_root / "deploy/install.sh";
    const int install_result =
        RunProcess({installer.string()}, installer.parent_path(),
                   {{"COCKPIT_ROOT", install_root.string()}, {"INSTALL_SYSTEMD", "false"}});
    if (install_result != 0) {
      std::string recovery_error;
      if (!RecoverInterruptedUpgrade(install_root, nullptr, &recovery_error)) {
        result->error = "package installer failed with exit code " +
                        std::to_string(install_result) + "; " + recovery_error;
        return false;
      }
      result->error = "package installer failed with exit code " + std::to_string(install_result);
      return false;
    }
    if (!std::filesystem::is_symlink(current) ||
        std::filesystem::read_symlink(current) !=
            std::filesystem::path("releases") / request.version) {
      result->error = "package installer did not activate expected release";
      std::string recovery_error;
      if (!RecoverInterruptedUpgrade(install_root, nullptr, &recovery_error)) {
        result->error += "; " + recovery_error;
      }
      return false;
    }
    transaction.state = TransactionState::kActivated;
    if (!SaveTransaction(transaction_path, transaction, &result->error)) {
      std::string recovery_error;
      if (!RecoverInterruptedUpgrade(install_root, nullptr, &recovery_error)) {
        result->error += "; " + recovery_error;
      }
      return false;
    }
    result->state = UpgradeState::kActivated;
    return true;
  } catch (const std::exception& exception) {
    result->error = "install upgrade failed: " + std::string(exception.what());
    return false;
  }
}

bool RollbackUpgrade(const std::filesystem::path& install_root,
                     const std::filesystem::path& previous_release, std::string* error) {
  try {
    const std::filesystem::path root = std::filesystem::absolute(install_root);
    const std::filesystem::path current = root / "current";
    const std::filesystem::path temporary = root / "current.new";
    std::filesystem::path activated_release;
    if (std::filesystem::is_symlink(current)) {
      activated_release = std::filesystem::read_symlink(current);
      if (!IsSafeReleaseTarget(activated_release)) {
        return Fail(error, "active release symlink target is unsafe");
      }
    }
    std::error_code filesystem_error;
    std::filesystem::remove(temporary, filesystem_error);
    if (previous_release.empty()) {
      std::filesystem::remove(current, filesystem_error);
      if (filesystem_error) {
        return Fail(error, "remove current release failed: " + filesystem_error.message());
      }
    } else {
      if (!IsSafeReleaseTarget(previous_release)) {
        return Fail(error, "previous release symlink target is unsafe");
      }
      if (!std::filesystem::is_directory(root / previous_release)) {
        return Fail(error, "previous release directory does not exist");
      }
      std::filesystem::create_symlink(previous_release, temporary);
      std::filesystem::rename(temporary, current);
    }
    if (!activated_release.empty() && activated_release != previous_release) {
      std::filesystem::remove_all(root / activated_release, filesystem_error);
      if (filesystem_error) {
        return Fail(error, "remove failed release failed: " + filesystem_error.message());
      }
    }
    return true;
  } catch (const std::exception& exception) {
    return Fail(error, "rollback upgrade failed: " + std::string(exception.what()));
  }
}

bool RecoverInterruptedUpgrade(const std::filesystem::path& install_root, bool* recovered,
                               std::string* error) {
  if (recovered != nullptr) {
    *recovered = false;
  }
  try {
    const std::filesystem::path root = std::filesystem::absolute(install_root);
    const std::filesystem::path transaction_path = root / "run/upgrade-transaction.yaml";
    const std::filesystem::file_status transaction_status =
        std::filesystem::symlink_status(transaction_path);
    if (!std::filesystem::exists(transaction_status)) {
      return true;
    }
    if (!std::filesystem::is_regular_file(transaction_status)) {
      return Fail(error, "upgrade transaction path is not a regular file");
    }

    UpgradeTransaction transaction;
    if (!LoadTransaction(transaction_path, &transaction, error)) {
      return false;
    }
    if (transaction.state == TransactionState::kConfirmed) {
      const std::filesystem::path current = root / "current";
      const std::filesystem::path candidate =
          std::filesystem::path("releases") / transaction.version;
      if (!std::filesystem::is_symlink(current) ||
          std::filesystem::read_symlink(current) != candidate ||
          !std::filesystem::is_directory(root / candidate)) {
        return Fail(error, "confirmed upgrade transaction does not match current release");
      }
    } else if (!RollbackUpgrade(root, transaction.previous_release, error)) {
      return false;
    }

    std::error_code filesystem_error;
    if (transaction.state != TransactionState::kConfirmed) {
      std::filesystem::remove_all(root / "releases" / transaction.version, filesystem_error);
      if (filesystem_error) {
        return Fail(error, "remove interrupted release failed: " + filesystem_error.message());
      }
    }
    std::filesystem::remove(transaction_path, filesystem_error);
    if (filesystem_error) {
      return Fail(error, "remove upgrade transaction failed: " + filesystem_error.message());
    }
    if (recovered != nullptr) {
      *recovered = true;
    }
    return true;
  } catch (const std::exception& exception) {
    return Fail(error, "recover interrupted upgrade failed: " + std::string(exception.what()));
  }
}

bool ConfirmUpgrade(const std::filesystem::path& install_root, const std::string& version,
                    std::string* error) {
  try {
    const std::filesystem::path root = std::filesystem::absolute(install_root);
    const std::filesystem::path transaction_path = root / "run/upgrade-transaction.yaml";
    UpgradeTransaction transaction;
    if (!LoadTransaction(transaction_path, &transaction, error)) {
      return false;
    }
    if (transaction.state != TransactionState::kActivated || transaction.version != version) {
      return Fail(error, "active upgrade transaction does not match confirmed version");
    }
    const std::filesystem::path expected_release =
        std::filesystem::path("releases") / transaction.version;
    const std::filesystem::path current = root / "current";
    if (!std::filesystem::is_symlink(current) ||
        std::filesystem::read_symlink(current) != expected_release) {
      return Fail(error, "confirmed release is not active");
    }

    transaction.state = TransactionState::kConfirmed;
    if (!SaveTransaction(transaction_path, transaction, error)) {
      return false;
    }
    std::error_code filesystem_error;
    std::filesystem::remove(transaction_path, filesystem_error);
    return filesystem_error
               ? Fail(error, "remove confirmed transaction failed: " + filesystem_error.message())
               : true;
  } catch (const std::exception& exception) {
    return Fail(error, "confirm upgrade failed: " + std::string(exception.what()));
  }
}

bool WaitForUpgradeHealth(const std::filesystem::path& command,
                          const std::filesystem::path& install_root, int timeout_seconds,
                          std::string* error) {
  if (timeout_seconds <= 0 || !std::filesystem::is_regular_file(command) ||
      access(command.c_str(), X_OK) != 0) {
    return Fail(error, "health command must be an executable file and timeout must be positive");
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
  do {
    if (RunProcess({std::filesystem::absolute(command).string()}, command.parent_path(),
                   {{"COCKPIT_ROOT", std::filesystem::absolute(install_root).string()}}) == 0) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  } while (std::chrono::steady_clock::now() < deadline);
  return Fail(error, "new release did not become healthy before timeout");
}

bool SaveUpgradeRequest(const std::filesystem::path& path, const UpgradeRequest& request,
                        std::string* error) {
  YAML::Emitter output;
  output << YAML::BeginMap << YAML::Key << "package_root" << YAML::Value
         << std::filesystem::absolute(request.package_root).string() << YAML::Key << "install_root"
         << YAML::Value << std::filesystem::absolute(request.install_root).string() << YAML::Key
         << "version" << YAML::Value << request.version << YAML::EndMap;
  return output.good() ? WriteYaml(path, output, error)
                       : Fail(error, "serialize upgrade request failed");
}

bool LoadUpgradeRequest(const std::filesystem::path& path, UpgradeRequest* request,
                        std::string* error) {
  if (request == nullptr) {
    return Fail(error, "upgrade request result must not be null");
  }
  try {
    const YAML::Node root = YAML::LoadFile(path.string());
    std::string package_root;
    std::string install_root;
    if (!root.IsMap() || !ReadRequiredScalar(root, "package_root", &package_root, error) ||
        !ReadRequiredScalar(root, "install_root", &install_root, error) ||
        !ReadRequiredScalar(root, "version", &request->version, error)) {
      return false;
    }
    request->package_root = package_root;
    request->install_root = install_root;
    return true;
  } catch (const std::exception& exception) {
    return Fail(error, "load upgrade request failed: " + std::string(exception.what()));
  }
}

bool SaveUpgradeResult(const std::filesystem::path& path, const UpgradeResult& result,
                       std::string* error) {
  YAML::Emitter output;
  output << YAML::BeginMap << YAML::Key << "state" << YAML::Value
         << (result.state == UpgradeState::kActivated ? "activated" : "failed") << YAML::Key
         << "version" << YAML::Value << result.version << YAML::Key << "previous_release"
         << YAML::Value << result.previous_release.string() << YAML::Key << "error" << YAML::Value
         << result.error << YAML::EndMap;
  return output.good() ? WriteYaml(path, output, error)
                       : Fail(error, "serialize upgrade result failed");
}

bool LoadUpgradeResult(const std::filesystem::path& path, UpgradeResult* result,
                       std::string* error) {
  if (result == nullptr) {
    return Fail(error, "upgrade result must not be null");
  }
  try {
    const YAML::Node root = YAML::LoadFile(path.string());
    std::string state;
    std::string previous_release;
    if (!root.IsMap() || !ReadRequiredScalar(root, "state", &state, error) ||
        !ReadRequiredScalar(root, "version", &result->version, error) ||
        !ReadRequiredScalar(root, "previous_release", &previous_release, error) ||
        !ReadRequiredScalar(root, "error", &result->error, error)) {
      return false;
    }
    if (state != "activated" && state != "failed") {
      return Fail(error, "upgrade result state is invalid");
    }
    result->state = state == "activated" ? UpgradeState::kActivated : UpgradeState::kFailed;
    result->previous_release = previous_release;
    return true;
  } catch (const std::exception& exception) {
    return Fail(error, "load upgrade result failed: " + std::string(exception.what()));
  }
}

}  // namespace upgrader
}  // namespace cockpit
