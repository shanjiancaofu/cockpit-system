#pragma once

#include <memory>

#include "cockpit/modules/voice/actions/action_dispatcher.h"
#include "cockpit/modules/voice/actions/hmi_command_provider.h"
#include "cockpit/modules/voice/actions/vehicle_status_provider.h"

namespace cockpit {
namespace voice {

class CockpitActionDispatcher final : public ActionDispatcher {
 public:
  explicit CockpitActionDispatcher(std::unique_ptr<VehicleStatusProvider> vehicle_status);
  CockpitActionDispatcher(std::unique_ptr<VehicleStatusProvider> vehicle_status,
                          std::unique_ptr<HmiCommandProvider> hmi_commands);

  ActionExecutionResult Execute(VoiceAction action) override;
  void Cancel() override;

 private:
  ActionExecutionResult QueryVehicleStatus();
  ActionExecutionResult SendHmiCommand(HmiCommand command, const char* not_configured_message);

  const std::unique_ptr<VehicleStatusProvider> vehicle_status_;
  const std::unique_ptr<HmiCommandProvider> hmi_commands_;
};

}  // namespace voice
}  // namespace cockpit
