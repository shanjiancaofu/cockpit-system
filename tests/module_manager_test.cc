#include <memory>
#include <string>
#include <vector>

#include "core/runtime/Module.h"
#include "core/runtime/ModuleManager.h"

namespace {

class TestModule : public cockpit::runtime::BasicModule {
 public:
  TestModule(std::string name, bool start_result, std::vector<std::string>* events)
      : BasicModule(std::move(name)), start_result_(start_result), events_(events) {
  }

 protected:
  bool OnStart() override {
    events_->push_back("start:" + name());
    return start_result_;
  }

  void OnStop() override {
    events_->push_back("stop:" + name());
  }

 private:
  bool start_result_{true};
  std::vector<std::string>* events_{nullptr};
};

bool Expect(bool condition, const char* message) {
  if (!condition) {
    return false;
  }
  return true;
}

}  // namespace

int main() {
  using cockpit::runtime::ModuleManager;
  using cockpit::runtime::ModuleState;

  std::vector<std::string> events;
  ModuleManager manager;
  manager.Add(std::make_unique<TestModule>("vehicle", true, &events));
  manager.Add(std::make_unique<TestModule>("camera", true, &events));

  if (!Expect(manager.StartAll(), "start all failed")) {
    return 1;
  }
  if (!Expect(manager.running(), "manager should be running")) {
    return 1;
  }

  const auto status = manager.Status();
  if (!Expect(status.size() == 2, "status size mismatch")) {
    return 1;
  }
  if (!Expect(status[0].name == "vehicle" && status[0].state == ModuleState::kRunning,
              "vehicle status mismatch")) {
    return 1;
  }
  if (!Expect(status[1].name == "camera" && status[1].state == ModuleState::kRunning,
              "camera status mismatch")) {
    return 1;
  }

  manager.StopAll();
  const std::vector<std::string> expected = {"start:vehicle", "start:camera", "stop:camera",
                                             "stop:vehicle"};
  if (!Expect(events == expected, "module order mismatch")) {
    return 1;
  }

  events.clear();
  ModuleManager rollback_manager;
  rollback_manager.Add(std::make_unique<TestModule>("audio", true, &events));
  rollback_manager.Add(std::make_unique<TestModule>("voice", false, &events));
  if (!Expect(!rollback_manager.StartAll(), "rollback manager should fail")) {
    return 1;
  }
  const std::vector<std::string> rollback_expected = {"start:audio", "start:voice", "stop:audio"};
  if (!Expect(events == rollback_expected, "rollback order mismatch")) {
    return 1;
  }

  return 0;
}
