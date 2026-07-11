#include "recording_ctl.h"

#include <cstdint>
#include <iostream>
#include <string>

#include "recording_control_client.h"

#include "cockpit/core/runtime/service_runtime.h"
#include "cockpit/core/utils/time.h"
#include "tools/diagnostics/cli_output.h"

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

const char* TimelineKindName(cockpit::proto::recording::RecordingTimelineEntryKind kind) {
  switch (kind) {
    case cockpit::proto::recording::RECORDING_TIMELINE_ENTRY_KIND_VEHICLE_STATE:
      return "vehicle_state";
    case cockpit::proto::recording::RECORDING_TIMELINE_ENTRY_KIND_EVENT:
      return "event";
    case cockpit::proto::recording::RECORDING_TIMELINE_ENTRY_KIND_DATA_FILE:
      return "data_file";
    case cockpit::proto::recording::RECORDING_TIMELINE_ENTRY_KIND_UNSPECIFIED:
    default:
      return "unspecified";
  }
}

void PrintTimeline(const cockpit::proto::recording::GetRecordingTimelineResponse& response) {
  std::cout << "total entries: " << response.total_entries() << '\n'
            << "returned entries: " << response.entries_size() << '\n'
            << "corrupted lines: " << response.corrupted_lines() << '\n'
            << "truncated: " << (response.truncated() ? "true" : "false") << '\n';
  for (const auto& entry : response.entries()) {
    std::cout << entry.timestamp_ms() << " kind=" << TimelineKindName(entry.kind())
              << " source=" << entry.source() << " label=" << entry.label();
    if (!entry.path().empty()) {
      std::cout << " path=" << entry.path();
    }
    std::cout << '\n';
  }
}

const char* IntegrityIssueKindName(cockpit::proto::recording::RecordingIntegrityIssueKind kind) {
  switch (kind) {
    case cockpit::proto::recording::RECORDING_INTEGRITY_ISSUE_KIND_INVALID_INDEX:
      return "invalid_index";
    case cockpit::proto::recording::RECORDING_INTEGRITY_ISSUE_KIND_UNSAFE_PATH:
      return "unsafe_path";
    case cockpit::proto::recording::RECORDING_INTEGRITY_ISSUE_KIND_MISSING_FILE:
      return "missing_file";
    case cockpit::proto::recording::RECORDING_INTEGRITY_ISSUE_KIND_NOT_REGULAR_FILE:
      return "not_regular_file";
    case cockpit::proto::recording::RECORDING_INTEGRITY_ISSUE_KIND_SIZE_MISMATCH:
      return "size_mismatch";
    case cockpit::proto::recording::RECORDING_INTEGRITY_ISSUE_KIND_CHECKSUM_MISMATCH:
      return "checksum_mismatch";
    case cockpit::proto::recording::RECORDING_INTEGRITY_ISSUE_KIND_UNSPECIFIED:
    default:
      return "unknown";
  }
}

void PrintVerification(const cockpit::proto::recording::VerifyRecordingResponse& response) {
  std::cout << "healthy: " << (response.healthy() ? "true" : "false") << '\n'
            << "index entries: " << response.index_entries() << '\n'
            << "files checked: " << response.files_checked() << '\n'
            << "checksums checked: " << response.checksums_checked() << '\n'
            << "checksums unavailable: " << response.checksums_unavailable() << '\n'
            << "issues: " << response.issues_size() << '\n';
  for (const auto& issue : response.issues()) {
    std::cout << "line=" << issue.line_number() << " kind=" << IntegrityIssueKindName(issue.kind())
              << " source=" << issue.source() << " path=" << issue.path() << " message=\""
              << issue.message() << "\"";
    if (issue.kind() == cockpit::proto::recording::RECORDING_INTEGRITY_ISSUE_KIND_SIZE_MISMATCH) {
      std::cout << " expected_size=" << issue.expected_size_bytes()
                << " actual_size=" << issue.actual_size_bytes();
    }
    if (issue.kind() ==
        cockpit::proto::recording::RECORDING_INTEGRITY_ISSUE_KIND_CHECKSUM_MISMATCH) {
      std::cout << " expected_checksum=" << issue.expected_checksum()
                << " actual_checksum=" << issue.actual_checksum();
    }
    std::cout << '\n';
  }
}

const char* SessionIntegrityStateName(
    cockpit::proto::recording::RecordingSessionIntegrityState state) {
  switch (state) {
    case cockpit::proto::recording::RECORDING_SESSION_INTEGRITY_STATE_HEALTHY:
      return "healthy";
    case cockpit::proto::recording::RECORDING_SESSION_INTEGRITY_STATE_DAMAGED:
      return "damaged";
    case cockpit::proto::recording::RECORDING_SESSION_INTEGRITY_STATE_UNAVAILABLE:
      return "unavailable";
    case cockpit::proto::recording::RECORDING_SESSION_INTEGRITY_STATE_UNSPECIFIED:
    default:
      return "unknown";
  }
}

void PrintBatchVerification(
    const cockpit::proto::recording::VerifyAllRecordingsResponse& response) {
  std::cout << "total sessions: " << response.total_sessions() << '\n'
            << "checked sessions: " << response.sessions_size() << '\n'
            << "healthy sessions: " << response.healthy_sessions() << '\n'
            << "damaged sessions: " << response.damaged_sessions() << '\n'
            << "unavailable sessions: " << response.unavailable_sessions() << '\n'
            << "truncated: " << (response.truncated() ? "true" : "false") << '\n';
  for (const auto& session : response.sessions()) {
    std::cout << session.session_id() << " state=" << SessionIntegrityStateName(session.state())
              << " started_at_ms=" << session.started_at_ms()
              << " files_checked=" << session.files_checked() << " issues=" << session.issues();
    if (!session.error().empty()) {
      std::cout << " error=\"" << session.error() << "\"";
    }
    std::cout << '\n';
  }
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

std::int64_t ParseInt64(const std::string& value) {
  if (value.empty()) {
    return 0;
  }
  try {
    return std::stoll(value);
  } catch (const std::exception&) {
    return -1;
  }
}

}  // namespace

int cockpit::recording_ctl::Run(int argc, char** argv) {
  const auto runtime = cockpit::runtime::ServiceRuntime::Create(argc, argv, "recording-ctl");
  cockpit::recording::RecordingControlClient client(
      runtime.config().services().recording.grpc.listen_address);
  cockpit::proto::recording::RecordingStatus status;
  std::string error;
  bool ok = false;
  const std::string delete_session_id = runtime.args().GetString("delete", "");
  const std::string detail_session_id = runtime.args().GetString("detail", "");
  const std::string timeline_session_id = runtime.args().GetString("timeline", "");
  const std::string verify_session_id = runtime.args().GetString("verify", "");
  const bool verify_all = runtime.args().HasFlag("verify-all");
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
  } else if (!timeline_session_id.empty()) {
    cockpit::proto::recording::GetRecordingTimelineRequest request;
    request.set_session_id(timeline_session_id);
    request.set_from_timestamp_ms(ParseInt64(runtime.args().GetString("from-ms", "0")));
    request.set_to_timestamp_ms(ParseInt64(runtime.args().GetString("to-ms", "0")));
    const int limit = runtime.args().GetInt("limit", 100);
    if (limit < 0) {
      std::cerr << "timeline limit must not be negative\n";
      return Finish(runtime, 1);
    }
    request.set_limit(static_cast<std::uint32_t>(limit));
    cockpit::proto::recording::GetRecordingTimelineResponse response;
    ok = client.GetTimeline(request, &response, &error);
    if (ok) {
      PrintTimeline(response);
    }
  } else if (!verify_session_id.empty()) {
    cockpit::proto::recording::VerifyRecordingResponse response;
    ok = client.Verify(verify_session_id, &response, &error);
    if (ok) {
      PrintVerification(response);
      if (!response.healthy()) {
        return Finish(runtime, 2);
      }
    }
  } else if (verify_all) {
    cockpit::diagnostics::OutputFormat output_format;
    if (!cockpit::diagnostics::ParseOutputFormat(runtime.args().GetString("output", "text"),
                                                 &output_format, &error)) {
      std::cerr << error << '\n';
      return Finish(runtime,
                    cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kInvalidArguments));
    }
    const std::int64_t from_ms = ParseInt64(runtime.args().GetString("from-started-ms", "0"));
    const std::int64_t to_ms = ParseInt64(runtime.args().GetString("to-started-ms", "0"));
    const int limit = runtime.args().GetInt("limit", 100);
    if (from_ms < 0 || to_ms < 0 || limit < 1 || limit > 1000 || (to_ms > 0 && from_ms > to_ms)) {
      constexpr char kMessage[] = "verify-all range or limit is invalid";
      if (output_format == cockpit::diagnostics::OutputFormat::kJson) {
        cockpit::diagnostics::WriteJsonError("invalid_arguments", kMessage, &std::cerr);
      } else {
        std::cerr << kMessage << '\n';
      }
      return Finish(runtime,
                    cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kInvalidArguments));
    }
    cockpit::proto::recording::VerifyAllRecordingsRequest request;
    request.set_from_started_at_ms(from_ms);
    request.set_to_started_at_ms(to_ms);
    request.set_limit(static_cast<std::uint32_t>(limit));
    cockpit::proto::recording::VerifyAllRecordingsResponse response;
    ok = client.VerifyAll(request, &response, &error);
    if (!ok) {
      const std::string message = error.empty() ? "recording control request failed" : error;
      if (output_format == cockpit::diagnostics::OutputFormat::kJson) {
        cockpit::diagnostics::WriteJsonError("operation_failed", message, &std::cerr);
      } else {
        std::cerr << message << '\n';
      }
      return Finish(runtime,
                    cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kOperationFailed));
    }
    if (output_format == cockpit::diagnostics::OutputFormat::kJson) {
      if (!cockpit::diagnostics::WriteJson(response, &std::cout, &error)) {
        cockpit::diagnostics::WriteJsonError("operation_failed", error, &std::cerr);
        return Finish(
            runtime, cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kOperationFailed));
      }
    } else {
      PrintBatchVerification(response);
    }
    if (response.damaged_sessions() > 0 || response.unavailable_sessions() > 0) {
      return Finish(runtime,
                    cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kUnhealthy));
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
      timeline_session_id.empty() && verify_session_id.empty() && !verify_all &&
      !runtime.args().HasFlag("prune")) {
    PrintStatus(status);
  }
  return Finish(runtime, 0);
}
