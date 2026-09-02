#include "cockpit/navigator/run_config/run_config.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
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
  using cockpit::navigator::ModuleId;
  using cockpit::navigator::RunMode;

  const cockpit::navigator::RunConfig config = cockpit::navigator::RunConfig::Default();
  bool success = true;
  success &= Expect(static_cast<std::uint16_t>(ModuleId::kTransfer) == 1 &&
                        static_cast<std::uint16_t>(ModuleId::kVehicleDriver) == 10 &&
                        static_cast<std::uint16_t>(ModuleId::kAgent) == 20 &&
                        static_cast<std::uint16_t>(ModuleId::kMedia) == 24 &&
                        static_cast<std::uint16_t>(ModuleId::kSentinel) == 25 &&
                        static_cast<std::uint16_t>(ModuleId::kBridge) == 26 &&
                        static_cast<std::uint16_t>(ModuleId::kUpgrader) == 30,
                    "stable module ids changed");
  success &= Expect(static_cast<std::uint8_t>(RunMode::kNormal) == 1 &&
                        static_cast<std::uint8_t>(RunMode::kDevelopment) == 2 &&
                        static_cast<std::uint8_t>(RunMode::kUpgrade) == 5,
                    "stable run mode ids changed");
  success &= Expect(config.initial_mode == RunMode::kNormal, "default mode mismatch");
  success &= Expect(config.startup_timeout_ms == 10000,
                    "default startup timeout does not cover Jetson model loading");
  success &=
      Expect(config.FindModule(ModuleId::kTransfer) != nullptr, "transfer module is missing");
  success &=
      Expect(config.FindModule(ModuleId::kCarupload) != nullptr, "carupload module is missing");
  success &=
      Expect(config.FindModule(ModuleId::kUpgrader) != nullptr, "upgrader skeleton is missing");
  success &= Expect(config.FindModule(ModuleId::kMedia) != nullptr, "media module is missing");
  success &=
      Expect(config.FindModule(ModuleId::kSentinel) != nullptr, "sentinel module is missing");
  success &= Expect(config.FindModule(ModuleId::kBridge) != nullptr, "bridge module is missing");
  ModuleId parsed_module;
  success &= Expect(cockpit::navigator::ParseModuleId("transfer", &parsed_module) &&
                        parsed_module == ModuleId::kTransfer,
                    "transfer module name was not parsed");
  success &= Expect(!cockpit::navigator::ParseModuleId("missing", &parsed_module),
                    "unknown module was resolved");

  const auto normal = config.modes.find(RunMode::kNormal);
  success &= Expect(normal != config.modes.end(), "normal mode is missing");
  if (normal != config.modes.end()) {
    success &= Expect(std::find(normal->second.begin(), normal->second.end(),
                                ModuleId::kTransfer) != normal->second.end(),
                      "normal mode does not contain transfer");
    success &= Expect(std::find(normal->second.begin(), normal->second.end(), ModuleId::kHmi) ==
                          normal->second.end(),
                      "normal mode unexpectedly contains hmi");
  }
  const auto ui = config.modes.find(RunMode::kUi);
  success &= Expect(ui != config.modes.end(), "ui mode is missing");
  if (ui != config.modes.end()) {
    success &=
        Expect(std::find(ui->second.begin(), ui->second.end(), ModuleId::kHmi) != ui->second.end(),
               "ui mode does not contain hmi");
    success &= Expect(
        std::find(ui->second.begin(), ui->second.end(), ModuleId::kMedia) != ui->second.end(),
        "ui mode does not contain media");
    success &= Expect(
        std::find(ui->second.begin(), ui->second.end(), ModuleId::kSentinel) != ui->second.end(),
        "ui mode does not contain sentinel");
  }
  const auto development = config.modes.find(RunMode::kDevelopment);
  success &= Expect(development != config.modes.end(), "development mode is missing");
  if (development != config.modes.end()) {
    success &= Expect(std::find(development->second.begin(), development->second.end(),
                                ModuleId::kBridge) != development->second.end(),
                      "development mode does not contain bridge");
  }
  const auto cloud = config.modes.find(RunMode::kCloud);
  success &= Expect(cloud != config.modes.end(), "cloud mode is missing");
  if (cloud != config.modes.end()) {
    success &= Expect(std::find(cloud->second.begin(), cloud->second.end(), ModuleId::kCarupload) !=
                          cloud->second.end(),
                      "cloud mode does not contain carupload");
  }
  const auto upgrade = config.modes.find(RunMode::kUpgrade);
  success &= Expect(upgrade != config.modes.end(), "upgrade mode is missing");
  if (upgrade != config.modes.end()) {
    success &= Expect(upgrade->second.size() == 1 && upgrade->second.front() == ModuleId::kUpgrader,
                      "upgrade mode must only contain upgrader");
  }
  RunMode parsed_mode;
  success &= Expect(cockpit::navigator::ParseRunMode("development", &parsed_mode) &&
                        parsed_mode == RunMode::kDevelopment,
                    "development mode name was not parsed");
  success &= Expect(!cockpit::navigator::ParseRunMode("broken", &parsed_mode),
                    "unknown mode was resolved");

  std::string validation_error;
  success &= Expect(config.Validate(&validation_error), "default run config is invalid");
  const cockpit::navigator::ModuleConfig* transfer = config.FindModule(ModuleId::kTransfer);
  const cockpit::navigator::ModuleConfig* hmi = config.FindModule(ModuleId::kHmi);
  success &= Expect(transfer != nullptr && transfer->critical, "transfer must be critical");
  success &= Expect(hmi != nullptr && !hmi->critical, "hmi must remain optional");

  cockpit::navigator::RunConfig invalid = config;
  invalid.modules.push_back(invalid.modules.front());
  success &= Expect(!invalid.Validate(&validation_error) &&
                        validation_error.find("duplicate Navigator module") != std::string::npos,
                    "duplicate module was accepted");

  invalid = config;
  invalid.modules.front().library = "/tmp/libtransfer.so";
  success &= Expect(!invalid.Validate(&validation_error) &&
                        validation_error.find("invalid library name") != std::string::npos,
                    "absolute module library path was accepted");

  invalid = config;
  invalid.modules.front().restart_limit = -1;
  success &= Expect(!invalid.Validate(&validation_error) &&
                        validation_error.find("invalid restart policy") != std::string::npos,
                    "negative restart limit was accepted");

  invalid = config;
  invalid.modes[RunMode::kNormal].push_back(ModuleId::kUnknown);
  success &=
      Expect(!invalid.Validate(&validation_error) &&
                 validation_error.find("references unconfigured module") != std::string::npos,
             "unconfigured mode module was accepted");
  return success ? 0 : 1;
}
