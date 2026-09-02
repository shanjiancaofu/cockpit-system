#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cockpit/navigator/common/module_id.h"
#include "cockpit/navigator/common/run_mode.h"

namespace cockpit {
namespace navigator {

struct ModuleConfig {
  ModuleConfig(ModuleId module_id, std::string library_name, bool is_critical = false,
               int module_restart_limit = 3, int module_restart_delay_ms = 200)
      : id(module_id),
        library(std::move(library_name)),
        critical(is_critical),
        restart_limit(module_restart_limit),
        restart_delay_ms(module_restart_delay_ms) {
  }

  ModuleId id;
  std::string library;
  bool critical{false};
  int restart_limit{3};
  int restart_delay_ms{200};
};

struct RunConfig {
  RunMode initial_mode{RunMode::kNormal};
  std::string socket_path;
  int startup_timeout_ms{10000};
  int stop_timeout_ms{3000};
  int restart_window_seconds{30};
  std::vector<ModuleConfig> modules;
  std::unordered_map<RunMode, std::vector<ModuleId>> modes;

  static RunConfig Default();
  bool Validate(std::string* error) const;
  const ModuleConfig* FindModule(ModuleId module) const;
  const std::vector<ModuleId>* FindMode(RunMode mode) const;
};

}  // namespace navigator
}  // namespace cockpit
