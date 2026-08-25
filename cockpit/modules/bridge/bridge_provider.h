#pragma once

#include "cockpit/modules/bridge/bridge.h"

namespace cockpit::bridge {

class BridgeProvider {
 public:
  virtual ~BridgeProvider() = default;
  virtual BridgeStatus SubmitGoal(const BridgeGoal& goal) = 0;
  virtual BridgeStatus CancelGoal(const std::string& goal_id) = 0;
  virtual BridgeStatus GetStatus() = 0;
};

}  // namespace cockpit::bridge
