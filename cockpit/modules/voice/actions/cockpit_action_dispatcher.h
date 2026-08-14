#pragma once

#include <chrono>
#include <memory>
#include <mutex>

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

  ActionExecutionResult Execute(VoiceAction action, const ActionExecutionContext& context) override;
  void Cancel() override;

 private:
  enum class ActiveProvider {
    kNone,
    kVehicle,
    kHmi,
  };

  ActionExecutionResult QueryVehicleStatus(const ActionExecutionContext& context);
  ActionExecutionResult SendHmiCommand(HmiCommand command, const char* not_configured_message,
                                       const ActionExecutionContext& context);
  void SetActiveProvider(ActiveProvider provider);
  void ClearActiveProvider(ActiveProvider provider);

  const std::unique_ptr<VehicleStatusProvider> vehicle_status_;
  const std::unique_ptr<HmiCommandProvider> hmi_commands_;
  std::mutex active_mutex_;
  ActiveProvider active_provider_ = ActiveProvider::kNone;
};

}  // namespace voice
}  // namespace cockpit
