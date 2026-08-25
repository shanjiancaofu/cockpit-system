#pragma once

#include "cockpit/modules/bridge/bridge.h"

namespace cockpit::bridge {

class NavigationProvider {
 public:
  virtual ~NavigationProvider() = default;
  virtual NavigationStatus SubmitNavigationGoal(const NavigationGoal& goal) = 0;
  virtual NavigationStatus CancelNavigationGoal(const std::string& goal_id) = 0;
  virtual NavigationStatus GetNavigationStatus() = 0;
};

}  // namespace cockpit::bridge
