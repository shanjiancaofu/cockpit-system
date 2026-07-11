#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>

#include "voice_control_client.h"

#include "cockpit/core/logging/Logger.h"
#include "cockpit/core/runtime/ServiceRuntime.h"
#include "tools/diagnostics/cli_output.h"

namespace {

int Finish(const cockpit::runtime::ServiceRuntime& runtime, int result) {
  runtime.MarkStopped();
  return result;
}

const char* StateName(cockpit::proto::voice::InteractionState state) {
  switch (state) {
    case cockpit::proto::voice::INTERACTION_STATE_DISABLED:
      return "disabled";
    case cockpit::proto::voice::INTERACTION_STATE_LISTENING:
      return "listening";
    case cockpit::proto::voice::INTERACTION_STATE_PROCESSING:
      return "processing";
    case cockpit::proto::voice::INTERACTION_STATE_FAULTED:
      return "faulted";
    case cockpit::proto::voice::INTERACTION_STATE_UNSPECIFIED:
      return "unspecified";
    default:
      return "unknown";
  }
}

void PrintResponseText(const cockpit::proto::voice::VoiceResponseEvent& response) {
  std::cout << "response id=" << response.id() << " transcript_id=" << response.transcript_id()
            << " intent=" << response.intent() << " action=" << response.action()
            << " action_status=" << response.action_status() << " transcript=\""
            << response.transcript_text() << "\""
            << " text=\"" << response.response_text() << "\""
            << " action_message=\"" << response.action_message() << "\"\n";
}

void PrintStatusText(const cockpit::proto::voice::VoiceInteractionStatus& status) {
  const auto& metrics = status.metrics();
  std::cout << "state: " << StateName(status.state()) << '\n'
            << "transcripts received: " << metrics.transcripts_received() << '\n'
            << "transcript events dropped: " << metrics.transcript_events_dropped() << '\n'
            << "responses published: " << metrics.responses_published() << '\n'
            << "unknown intents: " << metrics.unknown_intents() << '\n'
            << "processing errors: " << metrics.processing_errors() << '\n'
            << "upstream reconnects: " << metrics.upstream_reconnects() << '\n';
  std::cout << "actions attempted: " << metrics.actions_attempted() << '\n'
            << "actions succeeded: " << metrics.actions_succeeded() << '\n'
            << "actions failed: " << metrics.actions_failed() << '\n';
  std::cout << "speech requests accepted: " << metrics.speech_requests_accepted() << '\n'
            << "speech requests failed: " << metrics.speech_requests_failed() << '\n'
            << "speech requests dropped: " << metrics.speech_requests_dropped() << '\n'
            << "speech output available: " << (metrics.speech_output_available() ? "yes" : "no")
            << '\n'
            << "speech output reconnects: " << metrics.speech_output_reconnects() << '\n'
            << "speech output consecutive failures: "
            << metrics.speech_output_consecutive_failures() << '\n'
            << "speech output last success ms: "
            << metrics.speech_output_last_success_timestamp_ms() << '\n';
  if (status.has_latest_response()) {
    PrintResponseText(status.latest_response());
  }
  if (!status.last_error().empty()) {
    std::cout << "last error: " << status.last_error() << '\n';
  }
}

void PrintUsage() {
  std::cout << "usage:\n"
            << "  voice-ctl --status [--address HOST:PORT]\n"
            << "  voice-ctl --process TEXT [--address HOST:PORT]\n"
            << "  voice-ctl --responses [--after-id N] [--count N] "
               "[--timeout-ms N]\n"
            << "  all commands accept [--output text|json]\n";
}

bool PrintMessage(const google::protobuf::Message& message,
                  cockpit::diagnostics::OutputFormat format, std::string* error) {
  if (format == cockpit::diagnostics::OutputFormat::kJson) {
    return cockpit::diagnostics::WriteJson(message, &std::cout, error);
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  auto runtime = cockpit::runtime::ServiceRuntime::Create(argc, argv, "voice-ctl");
  cockpit::diagnostics::OutputFormat output_format;
  std::string error;
  if (!cockpit::diagnostics::ParseOutputFormat(runtime.args().GetString("output", "text"),
                                               &output_format, &error)) {
    std::cerr << error << '\n';
    return Finish(runtime,
                  cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kInvalidArguments));
  }
  const std::string text = runtime.args().GetString("process", "");
  const bool responses = runtime.args().HasFlag("responses");
  const bool status = runtime.args().HasFlag("status") || (text.empty() && !responses);
  const int command_count =
      static_cast<int>(status) + static_cast<int>(!text.empty()) + static_cast<int>(responses);
  if (command_count != 1) {
    PrintUsage();
    return Finish(runtime, 2);
  }

  const std::string address = runtime.args().GetString(
      "address", runtime.config().services().voice_interaction.grpc.listen_address);
  cockpit::voice::VoiceControlClient client(address);
  bool success = false;
  if (status) {
    cockpit::proto::voice::VoiceInteractionStatus result;
    success = client.GetStatus(&result, &error);
    if (success) {
      success = PrintMessage(result, output_format, &error);
      if (success && output_format == cockpit::diagnostics::OutputFormat::kText) {
        PrintStatusText(result);
      }
    }
  } else if (!text.empty()) {
    cockpit::proto::voice::VoiceResponseEvent result;
    success = client.ProcessTranscript(text, &result, &error);
    if (success) {
      success = PrintMessage(result, output_format, &error);
      if (success && output_format == cockpit::diagnostics::OutputFormat::kText) {
        PrintResponseText(result);
      }
    }
  } else {
    const int count = std::clamp(runtime.args().GetInt("count", 1), 1, 100);
    const int timeout_ms = std::clamp(runtime.args().GetInt("timeout-ms", 10000), 100, 60000);
    const int after_id = std::max(runtime.args().GetInt("after-id", 0), 0);
    success = client.SubscribeResponses(
        static_cast<std::uint64_t>(after_id), static_cast<std::uint32_t>(count), timeout_ms,
        [output_format](const cockpit::proto::voice::VoiceResponseEvent& response) {
          if (output_format == cockpit::diagnostics::OutputFormat::kJson) {
            std::string serialization_error;
            if (!cockpit::diagnostics::WriteJson(response, &std::cout, &serialization_error)) {
              cockpit::diagnostics::WriteJsonError("serialization_failed", serialization_error,
                                                   &std::cerr);
            }
          } else {
            PrintResponseText(response);
          }
        },
        &error);
  }
  if (!success) {
    if (output_format == cockpit::diagnostics::OutputFormat::kJson) {
      cockpit::diagnostics::WriteJsonError("operation_failed", error, &std::cerr);
    } else {
      LOG_ERROR("voice control RPC failed address=" + address + " error=" + error);
    }
    return Finish(runtime,
                  cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kOperationFailed));
  }
  return Finish(runtime, 0);
}
