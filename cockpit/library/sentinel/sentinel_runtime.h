#pragma once

#include <memory>
#include <string>

namespace cockpit {
namespace sentinel {

class SentinelRuntime final {
 public:
  SentinelRuntime();
  ~SentinelRuntime();
  SentinelRuntime(const SentinelRuntime&) = delete;
  SentinelRuntime& operator=(const SentinelRuntime&) = delete;
  bool Start(const std::string& config_path);
  void Stop();
  int Poll() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace sentinel
}  // namespace cockpit
