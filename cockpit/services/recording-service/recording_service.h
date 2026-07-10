#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "cockpit/modules/recording/recording_catalog.h"
#include "cockpit/modules/recording/recording_session.h"
#include "vehicle_state.pb.h"

namespace cockpit {
namespace recording {

class RecordingService {
 public:
  RecordingService(std::filesystem::path root_directory, std::string vehicle_id,
                   RecordingRetentionPolicy retention_policy, RecordingMetadata metadata = {});

  bool Initialize(std::string* error);
  bool Start(const std::string& trigger, std::string* error);
  bool Stop(std::string* error);
  std::vector<RecordingSessionInfo> List(std::size_t limit) const;
  bool GetDetail(const std::string& session_id, RecordingSessionDetail* detail,
                 std::string* error) const;
  bool Delete(const std::string& session_id, std::string* error);
  bool Prune(RecordingPruneResult* result, std::string* error);
  bool HandleEvent(const RecordingEvent& event, std::string* error);
  bool HandleDataFile(const RecordingDataFile& file, std::string* error);
  void HandleVehicleState(const proto::vehicle::VehicleState& state);
  RecordingStatus status() const;

 private:
  bool RefreshAndPrune(std::string* error);

  RecordingSession session_;
  RecordingCatalog catalog_;
  const RecordingRetentionPolicy retention_policy_;
};

}  // namespace recording
}  // namespace cockpit
