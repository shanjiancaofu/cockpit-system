#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace cockpit {
namespace navigator {

struct ModuleConfig {
  std::string name;
  std::string library;
};

struct RunConfig {
  std::string initial_mode;
  std::string socket_path;
  int startup_timeout_ms{3000};
  int stop_timeout_ms{3000};
  int max_restarts{3};
  int restart_window_seconds{30};
  std::vector<ModuleConfig> modules;
  std::unordered_map<std::string, std::vector<std::string>> modes;

  static RunConfig Default();
  const ModuleConfig* FindModule(const std::string& name) const;
};

}  // namespace navigator
}  // namespace cockpit
