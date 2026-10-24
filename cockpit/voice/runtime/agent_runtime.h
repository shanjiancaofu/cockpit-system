#pragma once

#include <memory>
#include <string>

namespace cockpit {
namespace agent {

class AgentRuntime {
 public:
  AgentRuntime();
  ~AgentRuntime();

  AgentRuntime(const AgentRuntime&) = delete;
  AgentRuntime& operator=(const AgentRuntime&) = delete;

  bool Start(const std::string& config_path, bool force_enable = false);
  void Stop();
  int Poll() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace agent
}  // namespace cockpit
