#include "core/runtime/ModuleManager.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/logging/Logger.h"

namespace cockpit {
namespace runtime {

ModuleManager::~ModuleManager() {
  StopAll();
}

void ModuleManager::Add(std::unique_ptr<Module> module) {
  if (!module) {
    throw std::invalid_argument("module is null");
  }
  if (running_) {
    throw std::logic_error("cannot add module after start");
  }
  modules_.push_back(std::move(module));
}

bool ModuleManager::StartAll() {
  if (running_) {
    return true;
  }

  running_modules_.clear();
  for (const auto& module : modules_) {
    LOG_INFO("starting module " + module->name());
    if (!module->Start()) {
      LOG_ERROR("module start failed name=" + module->name() +
                " state=" + ToString(module->state()));
      StopAll();
      return false;
    }
    running_modules_.push_back(module.get());
    LOG_INFO("module started name=" + module->name());
  }

  running_ = true;
  return true;
}

void ModuleManager::StopAll() {
  for (auto it = running_modules_.rbegin(); it != running_modules_.rend(); ++it) {
    Module* module = *it;
    LOG_INFO("stopping module " + module->name());
    module->Stop();
    LOG_INFO("module stopped name=" + module->name());
  }
  running_modules_.clear();
  running_ = false;
}

bool ModuleManager::running() const {
  return running_;
}

std::vector<ModuleStatus> ModuleManager::Status() const {
  std::vector<ModuleStatus> status;
  status.reserve(modules_.size());
  for (const auto& module : modules_) {
    status.push_back(ModuleStatus{module->name(), module->state()});
  }
  return status;
}

}  // namespace runtime
}  // namespace cockpit
