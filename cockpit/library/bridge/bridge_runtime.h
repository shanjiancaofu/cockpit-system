#pragma once

#include <memory>
#include <string>

namespace cockpit::bridge {

class BridgeRuntime final {
 public:
  BridgeRuntime();
  ~BridgeRuntime();
  BridgeRuntime(const BridgeRuntime&) = delete;
  BridgeRuntime& operator=(const BridgeRuntime&) = delete;
  bool Start(const std::string& config_path);
  void Stop();
  int Poll() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace cockpit::bridge
