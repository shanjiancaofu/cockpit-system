#include "modules/voice/cockpit_action_dispatcher.h"

#include <iostream>
#include <memory>
#include <string>

namespace {

class FakeVehicleStatusProvider final : public cockpit::voice::VehicleStatusProvider {
 public:
  explicit FakeVehicleStatusProvider(bool available) : available_(available) {
  }

  bool GetLatest(cockpit::voice::VehicleStatusSnapshot* status, std::string* error) override {
    if (!available_) {
      if (error != nullptr) {
        *error = "vehicle gateway unavailable";
      }
      return false;
    }
    status->timestamp_ms = 100;
    status->speed_kph = 42.5;
    status->gear = 3;
    status->soc_percent = 76;
    status->source = "test";
    return true;
  }

 private:
  bool available_;
};

class FakeHmiCommandProvider final : public cockpit::voice::HmiCommandProvider {
 public:
  explicit FakeHmiCommandProvider(bool available) : available_(available) {
  }

  bool SendCommand(cockpit::voice::HmiCommand command, std::string* response,
                   std::string* error) override {
    if (!available_) {
      if (error != nullptr) {
        *error = "hmi bridge unavailable";
      }
      return false;
    }
    if (response != nullptr) {
      *response = std::string("hmi accepted ") + cockpit::voice::ToString(command);
    }
    return true;
  }

 private:
  bool available_;
};

}  // namespace

int main() {
  using cockpit::voice::ActionExecutionStatus;
  using cockpit::voice::CockpitActionDispatcher;
  using cockpit::voice::VoiceAction;

  CockpitActionDispatcher dispatcher(std::make_unique<FakeVehicleStatusProvider>(true));
  const auto status = dispatcher.Execute(VoiceAction::kQueryVehicleStatus);
  if (status.status != ActionExecutionStatus::kSucceeded ||
      status.message.find("42.5") == std::string::npos ||
      status.message.find("76 percent") == std::string::npos) {
    std::cerr << "vehicle status action result is invalid\n";
    return 1;
  }
  if (dispatcher.Execute(VoiceAction::kOpenCamera).status !=
          ActionExecutionStatus::kNotImplemented ||
      dispatcher.Execute(static_cast<VoiceAction>(999)).status !=
          ActionExecutionStatus::kRejected) {
    std::cerr << "unsupported action policy is invalid\n";
    return 1;
  }

  CockpitActionDispatcher with_hmi(std::make_unique<FakeVehicleStatusProvider>(true),
                                   std::make_unique<FakeHmiCommandProvider>(true));
  const auto camera = with_hmi.Execute(VoiceAction::kOpenCamera);
  const auto music = with_hmi.Execute(VoiceAction::kPlayMusic);
  if (camera.status != ActionExecutionStatus::kSucceeded ||
      camera.message.find("open_camera_preview") == std::string::npos ||
      music.status != ActionExecutionStatus::kSucceeded ||
      music.message.find("play_music") == std::string::npos) {
    std::cerr << "hmi command dispatch failed\n";
    return 1;
  }

  CockpitActionDispatcher hmi_unavailable(std::make_unique<FakeVehicleStatusProvider>(true),
                                          std::make_unique<FakeHmiCommandProvider>(false));
  if (hmi_unavailable.Execute(VoiceAction::kOpenCamera).status != ActionExecutionStatus::kFailed) {
    std::cerr << "hmi command failure handling is invalid\n";
    return 1;
  }

  CockpitActionDispatcher unavailable(std::make_unique<FakeVehicleStatusProvider>(false));
  CockpitActionDispatcher missing(nullptr);
  if (unavailable.Execute(VoiceAction::kQueryVehicleStatus).status !=
          ActionExecutionStatus::kFailed ||
      missing.Execute(VoiceAction::kQueryVehicleStatus).status !=
          ActionExecutionStatus::kNotImplemented) {
    std::cerr << "vehicle status failure handling is invalid\n";
    return 1;
  }
  std::cout << "cockpit action dispatcher tests passed\n";
  return 0;
}
