#include "cockpit/core/runtime/Module.h"

#include <string>
#include <utility>

namespace cockpit {
namespace runtime {

const char* ToString(ModuleState state) {
  switch (state) {
    case ModuleState::kCreated:
      return "created";
    case ModuleState::kStarting:
      return "starting";
    case ModuleState::kRunning:
      return "running";
    case ModuleState::kStopping:
      return "stopping";
    case ModuleState::kStopped:
      return "stopped";
    case ModuleState::kFailed:
      return "failed";
  }
  return "unknown";
}

BasicModule::BasicModule(std::string name) : name_(std::move(name)) {
}

const std::string& BasicModule::name() const {
  return name_;
}

ModuleState BasicModule::state() const {
  return state_;
}

bool BasicModule::Start() {
  if (state_ == ModuleState::kRunning) {
    return true;
  }
  if (state_ == ModuleState::kStarting || state_ == ModuleState::kStopping) {
    return false;
  }

  state_ = ModuleState::kStarting;
  if (!OnStart()) {
    state_ = ModuleState::kFailed;
    return false;
  }

  state_ = ModuleState::kRunning;
  return true;
}

void BasicModule::Stop() {
  if (state_ == ModuleState::kStopped || state_ == ModuleState::kCreated) {
    state_ = ModuleState::kStopped;
    return;
  }
  if (state_ == ModuleState::kStopping) {
    return;
  }

  state_ = ModuleState::kStopping;
  OnStop();
  state_ = ModuleState::kStopped;
}

bool BasicModule::OnStart() {
  return true;
}

void BasicModule::OnStop() {
}

}  // namespace runtime
}  // namespace cockpit
