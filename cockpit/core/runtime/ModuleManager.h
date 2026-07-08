#pragma once

#include <memory>
#include <string>
#include <vector>

#include "cockpit/core/base/macros.h"
#include "cockpit/core/runtime/Module.h"

namespace cockpit {
namespace runtime {

struct ModuleStatus {
  std::string name;
  ModuleState state{ModuleState::kCreated};
};

class ModuleManager {
 public:
  ModuleManager() = default;
  ~ModuleManager();

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(ModuleManager);

  void Add(std::unique_ptr<Module> module);
  bool StartAll();
  void StopAll();

  bool running() const;
  std::vector<ModuleStatus> Status() const;

 private:
  std::vector<std::unique_ptr<Module>> modules_;
  std::vector<Module*> running_modules_;
  bool running_{false};
};

}  // namespace runtime
}  // namespace cockpit
