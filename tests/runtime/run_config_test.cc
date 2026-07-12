#include "cockpit/navigator/run_config/run_config.h"

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

bool Expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return false;
  }
  return true;
}

}  // namespace

int main() {
  const std::filesystem::path test_dir = std::filesystem::temp_directory_path() /
                                         ("cockpit-run-config-test-" + std::to_string(getpid()));
  std::filesystem::create_directories(test_dir);

  const std::filesystem::path valid_path = test_dir / "valid.yaml";
  std::ofstream valid(valid_path);
  valid << "initial_mode: normal\n"
        << "socket_path: /tmp/navigator-test.sock\n"
        << "modules:\n"
        << "  - name: transfer\n"
        << "    library: libtransfer.so\n"
        << "    config: system.yaml\n"
        << "modes:\n"
        << "  normal: [transfer]\n";
  valid.close();

  const cockpit::navigator::RunConfig config =
      cockpit::navigator::RunConfig::LoadFromFile(valid_path.string());
  bool success = true;
  success &= Expect(config.initial_mode == "normal", "initial mode mismatch");
  success &= Expect(config.modules.size() == 1, "module count mismatch");
  success &= Expect(config.modules[0].config_path == (test_dir / "system.yaml").string(),
                    "relative module config path was not resolved");

  const std::filesystem::path invalid_path = test_dir / "invalid.yaml";
  std::ofstream invalid(invalid_path);
  invalid << "initial_mode: normal\n"
          << "socket_path: /tmp/navigator-test.sock\n"
          << "modules:\n"
          << "  - name: transfer\n"
          << "    library: libtransfer.so\n"
          << "modes:\n"
          << "  normal: [missing]\n";
  invalid.close();

  try {
    cockpit::navigator::RunConfig::LoadFromFile(invalid_path.string());
    success &= Expect(false, "unknown mode module was accepted");
  } catch (const std::runtime_error&) {
  }

  std::filesystem::remove_all(test_dir);
  return success ? 0 : 1;
}
