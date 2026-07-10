#include <cstdint>
#include <iostream>
#include <string>

#include "recording_control_client.h"

#include "cockpit/core/runtime/ServiceRuntime.h"
#include "cockpit/core/utils/Time.h"

namespace {

const char* StateName(cockpit::proto::recording::RecordingState state) {
  switch (state) {
    case cockpit::proto::recording::RECORDING_STATE_IDLE:
      return "idle";
    case cockpit::proto::recording::RECORDING_STATE_RECORDING:
      return "recording";
    case cockpit::proto::recording::RECORDING_STATE_FAULTED:
      return "faulted";
    case cockpit::proto::recording::RECORDING_STATE_UNSPECIFIED:
    default:
      return "unspecified";
  }
}

void PrintStatus(const cockpit::proto::recording::RecordingStatus& status) {
  std::cout << "state: " << StateName(status.state()) << '\n'
            << "session id: " << status.session_id() << '\n'
            << "directory: " << status.directory() << '\n'
            << "trigger: " << status.trigger() << '\n'
            << "messages written: " << status.messages_written() << '\n'
            << "data files indexed: " << status.data_files_indexed() << '\n'
            << "started at ms: " << status.started_at_ms() << '\n'
            << "stopped at ms: " << status.stopped_at_ms() << '\n'
            << "last message timestamp ms: " << status.last_message_timestamp_ms() << '\n';
  std::cout << "stored sessions: " << status.stored_sessions() << '\n'
            << "stored bytes: " << status.stored_bytes() << '\n';
  if (!status.last_error().empty()) {
    std::cout << "last error: " << status.last_error() << '\n';
  }
}

void PrintDetail(const cockpit::proto::recording::RecordingSessionDetail& detail) {
  const auto& info = detail.info();
  std::cout << "session id: " << info.session_id() << '\n'
            << "state: " << info.state() << '\n'
            << "trigger: " << info.trigger() << '\n'
            << "directory: " << info.directory() << '\n'
            << "messages written: " << info.messages_written() << '\n'
            << "size bytes: " << info.size_bytes() << '\n'
            << "started at ms: " << info.started_at_ms() << '\n'
            << "stopped at ms: " << info.stopped_at_ms() << '\n'
            << "first message timestamp ms: " << detail.first_message_timestamp_ms() << '\n'
            << "last message timestamp ms: " << detail.last_message_timestamp_ms() << '\n'
            << "data files indexed: " << detail.data_files_indexed() << '\n'
            << "project: " << detail.project() << '\n'
            << "schema version: " << detail.schema_version() << '\n'
            << "vehicle id: " << detail.vehicle_id() << '\n'
            << "config path: " << detail.config_path() << '\n'
            << "config checksum: " << detail.config_checksum() << '\n'
            << "git commit: " << detail.git_commit() << '\n'
            << "git dirty: " << (detail.git_dirty() ? "true" : "false") << '\n'
            << "build type: " << detail.build_type() << '\n'
            << "binary version: " << detail.binary_version() << '\n';
  std::cout << "sources:";
  for (const auto& source : detail.sources()) {
    std::cout << ' ' << source;
  }
  std::cout << '\n';
}

int Finish(const cockpit::runtime::ServiceRuntime& runtime, int result) {
  runtime.MarkStopped();
  return result;
}

std::uint64_t ParseUint64(const std::string& value) {
  if (value.empty()) {
    return 0;
  }
  try {
    return static_cast<std::uint64_t>(std::stoull(value));
  } catch (const std::exception&) {
    return 0;
  }
}

}  // namespace

int main(int argc, char** argv) {
  const auto runtime = cockpit::runtime::ServiceRuntime::Create(argc, argv, "recording-ctl");
  cockpit::recording::RecordingControlClient client(
      runtime.config().services().recording.grpc.listen_address);
  cockpit::proto::recording::RecordingStatus status;
  std::string error;
  bool ok = false;
  const std::string delete_session_id = runtime.args().GetString("delete", "");
  const std::string detail_session_id = runtime.args().GetString("detail", "");
  const std::string event_topic = runtime.args().GetString("event-topic", "");
  const std::string file_path = runtime.args().GetString("file-path", "");
  if (runtime.args().HasFlag("list")) {
    cockpit::proto::recording::ListRecordingsResponse response;
    const int limit = runtime.args().GetInt("limit", 0);
    ok = client.List(limit > 0 ? static_cast<std::uint32_t>(limit) : 0, &response, &error);
    if (ok) {
      std::cout << "total sessions: " << response.total_sessions() << '\n'
                << "total bytes: " << response.total_bytes() << '\n';
      for (const auto& session : response.sessions()) {
        std::cout << session.session_id() << " state=" << session.state()
                  << " trigger=" << session.trigger() << " messages=" << session.messages_written()
                  << " data_files=" << session.data_files_indexed()
                  << " bytes=" << session.size_bytes()
                  << " started_at_ms=" << session.started_at_ms()
                  << " directory=" << session.directory() << '\n';
      }
    }
  } else if (!detail_session_id.empty()) {
    cockpit::proto::recording::RecordingSessionDetail detail;
    ok = client.GetDetail(detail_session_id, &detail, &error);
    if (ok) {
      PrintDetail(detail);
    }
  } else if (!delete_session_id.empty()) {
    ok = client.Delete(delete_session_id, &error);
    if (ok) {
      std::cout << "recording deleted\n";
    }
  } else if (runtime.args().HasFlag("prune")) {
    cockpit::proto::recording::PruneRecordingsResponse response;
    ok = client.Prune(&response, &error);
    if (ok) {
      std::cout << "sessions deleted: " << response.sessions_deleted() << '\n'
                << "bytes deleted: " << response.bytes_deleted() << '\n'
                << "sessions remaining: " << response.sessions_remaining() << '\n'
                << "bytes remaining: " << response.bytes_remaining() << '\n';
    }
  } else if (runtime.args().HasFlag("start")) {
    ok = client.Start(runtime.args().GetString("trigger", "manual"), &status, &error);
  } else if (runtime.args().HasFlag("stop")) {
    ok = client.Stop(&status, &error);
  } else if (!event_topic.empty()) {
    const std::string payload = runtime.args().GetString("event-payload", "{}");
    ok = client.AppendEvent(cockpit::utils::NowMs(), event_topic, payload, &status, &error);
  } else if (!file_path.empty()) {
    cockpit::proto::recording::AppendRecordingDataFileRequest request;
    request.set_timestamp_ms(cockpit::utils::NowMs());
    request.set_source(runtime.args().GetString("file-source", "manual"));
    request.set_kind(runtime.args().GetString("file-kind", "artifact"));
    request.set_path(file_path);
    request.set_size_bytes(ParseUint64(runtime.args().GetString("file-size-bytes", "0")));
    request.set_checksum(runtime.args().GetString("file-checksum", ""));
    request.set_copy_into_session(runtime.args().HasFlag("copy-into-session"));
    ok = client.AppendDataFile(request, &status, &error);
  } else {
    ok = client.GetStatus(&status, &error);
  }
  if (!ok) {
    std::cerr << (error.empty() ? "recording control request failed" : error) << '\n';
    return Finish(runtime, 1);
  }
  if (!runtime.args().HasFlag("list") && detail_session_id.empty() && delete_session_id.empty() &&
      !runtime.args().HasFlag("prune")) {
    PrintStatus(status);
  }
  return Finish(runtime, 0);
}
