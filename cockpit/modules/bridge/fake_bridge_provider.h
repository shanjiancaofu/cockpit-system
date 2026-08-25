#pragma once

#include <memory>
#include <string>

#include "cockpit/modules/bridge/bridge_provider.h"

namespace cockpit::bridge {

enum class FakeBridgeOutcome {
  kSucceeded,
  kRejected,
  kFailed,
  kStalled,
  kDisconnected,
};

bool ParseFakeBridgeOutcome(const std::string& value, FakeBridgeOutcome* outcome);
std::unique_ptr<BridgeProvider> CreateFakeBridgeProvider(FakeBridgeOutcome outcome);
std::unique_ptr<BridgeProvider> CreateDisabledBridgeProvider();

}  // namespace cockpit::bridge
