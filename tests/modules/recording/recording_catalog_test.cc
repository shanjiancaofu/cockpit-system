#include "cockpit/modules/recording/recording_catalog.h"

#include <unistd.h>

#include <filesystem>
#include <iostream>
#include <string>

#include "cockpit/modules/recording/recording_session.h"

namespace {

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

bool CreateSession(const std::filesystem::path& root, std::int64_t timestamp, std::string* error) {
  cockpit::recording::RecordingMetadata metadata;
  metadata.config_path = "configs/catalog.yaml";
  metadata.config_checksum = "fnv1a64:catalog";
  metadata.git_commit = "catalog_commit";
  metadata.build_type = "Debug";
  metadata.binary_version = "0.1.0";
  metadata.sources = {"vehicle_state", "events"};
  cockpit::recording::RecordingSession session(root, "catalog_test_vehicle", metadata);
  if (!session.Start("catalog_test", error)) {
    return false;
  }
  cockpit::vehicle::VehicleState state;
  state.timestamp_ms = timestamp;
  state.source = "test";
  cockpit::recording::RecordingDataFile data_file;
  data_file.timestamp_ms = timestamp + 1;
  data_file.source = "camera";
  data_file.kind = "jpeg";
  data_file.path = "photos/test.jpg";
  return session.Append(state, error) && session.AppendDataFile(data_file, error) &&
         session.Stop(error);
}

}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("cockpit_recording_catalog_test_" + std::to_string(getpid()));
  std::filesystem::remove_all(root);
  std::string error;
  if (!Check(CreateSession(root, 1000, &error), "create first session failed") ||
      !Check(CreateSession(root, 2000, &error), "create second session failed")) {
    std::cerr << error << '\n';
    return 1;
  }

  cockpit::recording::RecordingCatalog catalog(root);
  if (!Check(catalog.Refresh(&error), "refresh recording catalog failed")) {
    std::cerr << error << '\n';
    return 1;
  }
  const auto initial = catalog.List();
  cockpit::recording::RecordingSessionDetail detail;
  const bool detail_ok =
      Check(catalog.GetDetail(initial.front().session_id, &detail, &error),
            "recording catalog detail failed") &&
      Check(detail.info.session_id == initial.front().session_id,
            "recording catalog detail session mismatch") &&
      Check(detail.vehicle_id == "catalog_test_vehicle",
            "recording catalog detail vehicle id missing") &&
      Check(detail.config_checksum == "fnv1a64:catalog",
            "recording catalog detail config checksum missing") &&
      Check(detail.git_commit == "catalog_commit", "recording catalog detail git commit missing") &&
      Check(detail.build_type == "Debug", "recording catalog detail build type missing") &&
      Check(detail.binary_version == "0.1.0", "recording catalog detail binary version missing") &&
      Check(detail.sources.size() == 2, "recording catalog detail sources missing") &&
      Check(detail.data_files_indexed == 1, "recording catalog detail data file count missing");
  if (!detail_ok) {
    std::cerr << error << '\n';
    return 1;
  }
  cockpit::recording::RecordingPruneResult prune_result;
  const bool initial_ok =
      Check(initial.size() == 2, "recording catalog session count mismatch") &&
      Check(initial[0].started_at_ms >= initial[1].started_at_ms,
            "recording catalog sort order mismatch") &&
      Check(catalog.total_bytes() > 0, "recording catalog byte count missing") &&
      Check(catalog.Prune({1, 0}, &prune_result, &error), "recording catalog prune failed") &&
      Check(prune_result.sessions_deleted == 1, "recording catalog prune count mismatch") &&
      Check(catalog.List().size() == 1, "recording catalog remaining count mismatch");
  if (!initial_ok) {
    std::cerr << error << '\n';
    return 1;
  }

  const std::string remaining_id = catalog.List().front().session_id;
  const auto corrupted = root / "sessions" / "corrupted_test";
  std::filesystem::create_directories(corrupted);
  std::ofstream(corrupted / "COMPLETE") << "invalid\n";
  std::ofstream(corrupted / "manifest.json") << "not: [valid\n";
  const bool result =
      Check(catalog.Delete(remaining_id, &error), "recording catalog delete failed") &&
      Check(catalog.List().empty(), "recording catalog did not remove deleted session") &&
      Check(!catalog.Delete(remaining_id, &error),
            "missing recording session deletion succeeded") &&
      Check(catalog.Refresh(&error), "catalog refresh failed for corrupted manifest") &&
      Check(catalog.List().size() == 1 && catalog.List().front().state == "corrupted",
            "corrupted recording session was not exposed");
  std::filesystem::remove_all(root);
  return result ? 0 : 1;
}
