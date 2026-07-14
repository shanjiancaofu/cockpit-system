#pragma once

#include <filesystem>
#include <string>

namespace cockpit {
namespace safe_ota {

struct SafeOtaOptions {
  std::filesystem::path package_root;
  std::filesystem::path install_root;
  std::filesystem::path health_command;
  std::string confirmed_version;
  std::string socket_path;
  int timeout_seconds{60};
  bool standalone{false};
  bool recover_only{false};
};

}  // namespace safe_ota
}  // namespace cockpit
