#pragma once

#include "modules/voice/action_dispatcher.h"
#include "modules/voice/vehicle_status_provider.h"

#include <memory>

namespace cockpit {
namespace voice {

class CockpitActionDispatcher final : public ActionDispatcher {
 public:
  explicit CockpitActionDispatcher(
      std::unique_ptr<VehicleStatusProvider> vehicle_status);

  ActionExecutionResult Execute(VoiceAction action) override;

 private:
  ActionExecutionResult QueryVehicleStatus();

  const std::unique_ptr<VehicleStatusProvider> vehicle_status_;
};

}  // namespace voice
}  // namespace cockpit
