#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <thread>

namespace cockpit {
namespace upgrader {

class UpgraderRuntime {
 public:
  UpgraderRuntime() = default;
  ~UpgraderRuntime();

  UpgraderRuntime(const UpgraderRuntime&) = delete;
  UpgraderRuntime& operator=(const UpgraderRuntime&) = delete;

  bool Start(const std::string& config_path);
  void Stop();
  int Poll() const;

 private:
  std::filesystem::path request_path_;
  std::filesystem::path result_path_;
  std::thread worker_;
  std::atomic_bool active_{false};
  std::atomic_int result_{0};
};

}  // namespace upgrader
}  // namespace cockpit
