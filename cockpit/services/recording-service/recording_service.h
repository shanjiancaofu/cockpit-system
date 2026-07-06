#pragma once

#include <filesystem>
#include <string>

#include "cockpit/modules/recording/recording_session.h"
#include "vehicle_state.pb.h"

namespace cockpit {
namespace recording {

class RecordingService {
 public:
  RecordingService(std::filesystem::path root_directory, std::string vehicle_id);

  bool Start(const std::string& trigger, std::string* error);
  bool Stop(std::string* error);
  void HandleVehicleState(const proto::vehicle::VehicleState& state);
  RecordingStatus status() const;

 private:
  RecordingSession session_;
};

}  // namespace recording
}  // namespace cockpit
