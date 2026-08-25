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
std::unique_ptr<NavigationProvider> CreateFakeNavigationProvider(FakeBridgeOutcome outcome);
std::unique_ptr<NavigationProvider> CreateDisabledNavigationProvider();

}  // namespace cockpit::bridge
