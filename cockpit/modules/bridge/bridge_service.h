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

  BridgeService(std::unique_ptr<NavigationProvider> provider, std::int64_t goal_timeout_ms,
                Clock clock = nullptr);

  bool SubmitNavigationGoal(const NavigationGoal& goal, NavigationStatus* status,
                            std::string* error);
  bool CancelNavigationGoal(const std::string& goal_id, NavigationStatus* status,
                            std::string* error);
  NavigationStatus GetNavigationStatus();

 private:
  static bool ValidateGoal(const NavigationGoal& goal, std::string* error);
  void RefreshLocked();

  std::unique_ptr<NavigationProvider> provider_;
  const std::int64_t goal_timeout_ms_;
  const Clock clock_;
  std::mutex mutex_;
  NavigationStatus status_;
};

}  // namespace cockpit::bridge
