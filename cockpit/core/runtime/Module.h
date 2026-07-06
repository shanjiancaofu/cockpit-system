#pragma once

#include <string>

namespace cockpit {
namespace runtime {

enum class ModuleState {
  kCreated,
  kStarting,
  kRunning,
  kStopping,
  kStopped,
  kFailed,
};

const char* ToString(ModuleState state);

class Module {
 public:
  virtual ~Module() = default;

  virtual const std::string& name() const = 0;
  virtual ModuleState state() const = 0;
  virtual bool Start() = 0;
  virtual void Stop() = 0;
};

class BasicModule : public Module {
 public:
  explicit BasicModule(std::string name);
  ~BasicModule() override = default;

  const std::string& name() const override;
  ModuleState state() const override;
  bool Start() override;
  void Stop() override;

 protected:
  virtual bool OnStart();
  virtual void OnStop();

 private:
  std::string name_;
  ModuleState state_{ModuleState::kCreated};
};

}  // namespace runtime
}  // namespace cockpit
