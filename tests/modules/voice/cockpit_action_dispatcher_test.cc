#include "cockpit/modules/voice/actions/cockpit_action_dispatcher.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <string>

namespace {

class FakeVehicleStatusProvider final : public cockpit::voice::VehicleStatusProvider {
 public:
  explicit FakeVehicleStatusProvider(bool available) : available_(available) {
  }

  bool GetLatest(std::chrono::steady_clock::time_point deadline,
                 cockpit::voice::VehicleStatusSnapshot* status, std::string* error) override {
    deadline_observed_ = deadline > std::chrono::steady_clock::now();
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

  bool deadline_observed() const {
    return deadline_observed_;
  }

 private:
  bool available_;
  bool deadline_observed_ = false;
};

class FakeHmiCommandProvider final : public cockpit::voice::HmiCommandProvider {
 public:
  explicit FakeHmiCommandProvider(bool available) : available_(available) {
  }

  bool SendCommand(cockpit::voice::HmiCommand command,
                   std::chrono::steady_clock::time_point deadline, std::string* response,
                   std::string* error) override {
    deadline_observed_ = deadline > std::chrono::steady_clock::now();
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

  bool deadline_observed() const {
    return deadline_observed_;
  }

 private:
  bool available_;
  bool deadline_observed_ = false;
};

}  // namespace

int main() {
  using cockpit::voice::ActionExecutionStatus;
  using cockpit::voice::CockpitActionDispatcher;
  using cockpit::voice::VoiceAction;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);

  auto vehicle = std::make_unique<FakeVehicleStatusProvider>(true);
  auto* vehicle_observer = vehicle.get();
  CockpitActionDispatcher dispatcher(std::move(vehicle));
  const auto status = dispatcher.Execute(VoiceAction::kQueryVehicleStatus, deadline);
  if (status.status != ActionExecutionStatus::kSucceeded ||
      status.message.find("42.5") == std::string::npos ||
      status.message.find("76 percent") == std::string::npos ||
      !vehicle_observer->deadline_observed()) {
    std::cerr << "vehicle status action result is invalid\n";
    return 1;
  }
  if (dispatcher.Execute(VoiceAction::kOpenCamera, deadline).status !=
          ActionExecutionStatus::kNotImplemented ||
      dispatcher.Execute(static_cast<VoiceAction>(999), deadline).status !=
          ActionExecutionStatus::kRejected) {
    std::cerr << "unsupported action policy is invalid\n";
    return 1;
  }

  auto hmi = std::make_unique<FakeHmiCommandProvider>(true);
  auto* hmi_observer = hmi.get();
  CockpitActionDispatcher with_hmi(std::make_unique<FakeVehicleStatusProvider>(true),
                                   std::move(hmi));
  const auto camera = with_hmi.Execute(VoiceAction::kOpenCamera, deadline);
  const auto music = with_hmi.Execute(VoiceAction::kPlayMusic, deadline);
  if (camera.status != ActionExecutionStatus::kSucceeded ||
      camera.message.find("open_camera_preview") == std::string::npos ||
      music.status != ActionExecutionStatus::kSucceeded ||
      music.message.find("play_music") == std::string::npos || !hmi_observer->deadline_observed()) {
    std::cerr << "hmi command dispatch failed\n";
    return 1;
  }

  CockpitActionDispatcher hmi_unavailable(std::make_unique<FakeVehicleStatusProvider>(true),
                                          std::make_unique<FakeHmiCommandProvider>(false));
  if (hmi_unavailable.Execute(VoiceAction::kOpenCamera, deadline).status !=
      ActionExecutionStatus::kFailed) {
    std::cerr << "hmi command failure handling is invalid\n";
    return 1;
  }

  CockpitActionDispatcher unavailable(std::make_unique<FakeVehicleStatusProvider>(false));
  CockpitActionDispatcher missing(nullptr);
  if (unavailable.Execute(VoiceAction::kQueryVehicleStatus, deadline).status !=
          ActionExecutionStatus::kFailed ||
      missing.Execute(VoiceAction::kQueryVehicleStatus, deadline).status !=
          ActionExecutionStatus::kNotImplemented) {
    std::cerr << "vehicle status failure handling is invalid\n";
    return 1;
  }
  std::cout << "cockpit action dispatcher tests passed\n";
  return 0;
}
