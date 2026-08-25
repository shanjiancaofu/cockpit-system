#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "cockpit/modules/bridge/bridge_provider.h"

namespace cockpit::bridge {

class BridgeService final {
 public:
  using Clock = std::function<std::int64_t()>;

  BridgeService(std::unique_ptr<BridgeProvider> provider, std::int64_t goal_timeout_ms,
                Clock clock = nullptr);

  bool SubmitGoal(const BridgeGoal& goal, BridgeStatus* status, std::string* error);
  bool CancelGoal(const std::string& goal_id, BridgeStatus* status, std::string* error);
  BridgeStatus GetStatus();

 private:
  static bool ValidateGoal(const BridgeGoal& goal, std::string* error);
  void RefreshLocked();

  std::unique_ptr<BridgeProvider> provider_;
  const std::int64_t goal_timeout_ms_;
  const Clock clock_;
  std::mutex mutex_;
  BridgeStatus status_;
};

}  // namespace cockpit::bridge
