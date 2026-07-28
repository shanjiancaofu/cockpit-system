#include "cockpit/navigator/run_config/run_config.h"

#include <algorithm>
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
  const cockpit::navigator::RunConfig config = cockpit::navigator::RunConfig::Default();
  bool success = true;
  success &= Expect(config.initial_mode == "normal", "default mode mismatch");
  success &= Expect(config.FindModule("transfer") != nullptr, "transfer module is missing");
  success &= Expect(config.FindModule("carupload") != nullptr, "carupload module is missing");
  success &= Expect(config.FindModule("upgrader") != nullptr, "upgrader skeleton is missing");
  success &= Expect(config.FindModule("missing") == nullptr, "unknown module was resolved");

  const auto normal = config.modes.find("normal");
  success &= Expect(normal != config.modes.end(), "normal mode is missing");
  if (normal != config.modes.end()) {
    success &= Expect(
        std::find(normal->second.begin(), normal->second.end(), "transfer") != normal->second.end(),
        "normal mode does not contain transfer");
    success &= Expect(
        std::find(normal->second.begin(), normal->second.end(), "hmi") == normal->second.end(),
        "normal mode unexpectedly contains hmi");
  }
  const auto ui = config.modes.find("ui");
  success &= Expect(ui != config.modes.end(), "ui mode is missing");
  if (ui != config.modes.end()) {
    success &= Expect(std::find(ui->second.begin(), ui->second.end(), "hmi") != ui->second.end(),
                      "ui mode does not contain hmi");
  }
  const auto cloud = config.modes.find("cloud");
  success &= Expect(cloud != config.modes.end(), "cloud mode is missing");
  if (cloud != config.modes.end()) {
    success &= Expect(
        std::find(cloud->second.begin(), cloud->second.end(), "carupload") != cloud->second.end(),
        "cloud mode does not contain carupload");
  }
  const auto upgrade = config.modes.find("upgrade");
  success &= Expect(upgrade != config.modes.end(), "upgrade mode is missing");
  if (upgrade != config.modes.end()) {
    success &= Expect(upgrade->second.size() == 1 && upgrade->second.front() == "upgrader",
                      "upgrade mode must only contain upgrader");
  }
  return success ? 0 : 1;
}
