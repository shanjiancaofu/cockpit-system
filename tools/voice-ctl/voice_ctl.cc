#include "voice_ctl.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>

#include "voice_control_client.h"

#include "cockpit/core/logging/logger.h"
#include "cockpit/core/runtime/process_runtime.h"
#include "tools/diagnostics/cli_output.h"

namespace {

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

void PrintTranscriptText(const cockpit::proto::voice::TranscriptEvent& transcript) {
  std::cout << "transcript id=" << transcript.id() << " provider=" << transcript.provider()
            << " confidence=" << transcript.confidence()
            << " duration_ms=" << transcript.duration_ms()
            << " truncated=" << (transcript.truncated() ? "true" : "false")
            << " discontinuous=" << (transcript.discontinuous() ? "true" : "false") << " text=\""
            << transcript.text() << "\"\n";
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
            << "actions failed: " << metrics.actions_failed() << '\n'
            << "requests interrupted: " << metrics.requests_interrupted() << '\n'
            << "provider timeouts: " << metrics.provider_timeouts() << '\n'
            << "provider failures: " << metrics.provider_failures() << '\n';
  std::cout << "speech requests accepted: " << metrics.speech_requests_accepted() << '\n'
            << "speech requests failed: " << metrics.speech_requests_failed() << '\n'
            << "speech requests dropped: " << metrics.speech_requests_dropped() << '\n'
            << "audio playback available: " << (metrics.audio_playback_available() ? "yes" : "no")
            << '\n'
            << "audio playback reconnects: " << metrics.audio_playback_reconnects() << '\n'
            << "audio playback consecutive failures: "
            << metrics.audio_playback_consecutive_failures() << '\n'
            << "audio playback last success ms: "
            << metrics.audio_playback_last_success_timestamp_ms() << '\n';
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
            << "  voice-ctl --interrupt [--address HOST:PORT]\n"
            << "  voice-ctl --transcripts [--after-id N] [--count N] "
               "[--timeout-ms N]\n"
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

int cockpit::voice_ctl::ControlVoice(const cockpit::runtime::ProcessRuntime& runtime) {
  cockpit::diagnostics::OutputFormat output_format;
  std::string error;
  if (!cockpit::diagnostics::ParseOutputFormat(runtime.args().GetString("output", "text"),
                                               &output_format, &error)) {
    std::cerr << error << '\n';
    return cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kInvalidArguments);
  }
  const std::string text = runtime.args().GetString("process", "");
  const bool transcripts = runtime.args().HasFlag("transcripts");
  const bool responses = runtime.args().HasFlag("responses");
  const bool interrupt = runtime.args().HasFlag("interrupt");
  const bool status = runtime.args().HasFlag("status") ||
                      (text.empty() && !transcripts && !responses && !interrupt);
  const int command_count = static_cast<int>(status) + static_cast<int>(!text.empty()) +
                            static_cast<int>(transcripts) + static_cast<int>(responses) +
                            static_cast<int>(interrupt);
  if (command_count != 1) {
    PrintUsage();
    return 2;
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
  } else if (interrupt) {
    cockpit::proto::voice::InterruptVoiceResponse result;
    success = client.Interrupt(&result, &error);
    if (success) {
      success = PrintMessage(result, output_format, &error);
      if (success && output_format == cockpit::diagnostics::OutputFormat::kText) {
        std::cout << "active request interrupted: "
                  << (result.active_request_interrupted() ? "yes" : "no") << '\n'
                  << "queued transcripts discarded: " << result.queued_transcripts_discarded()
                  << '\n';
      }
    }
  } else if (responses) {
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
  } else {
    const int count = std::clamp(runtime.args().GetInt("count", 1), 1, 100);
    const int timeout_ms = std::clamp(runtime.args().GetInt("timeout-ms", 10000), 100, 60000);
    const int after_id = std::max(runtime.args().GetInt("after-id", 0), 0);
    success = client.SubscribeTranscripts(
        static_cast<std::uint64_t>(after_id), static_cast<std::uint32_t>(count), timeout_ms,
        [output_format](const cockpit::proto::voice::TranscriptEvent& transcript) {
          if (output_format == cockpit::diagnostics::OutputFormat::kJson) {
            std::string serialization_error;
            if (!cockpit::diagnostics::WriteJson(transcript, &std::cout, &serialization_error)) {
              cockpit::diagnostics::WriteJsonError("serialization_failed", serialization_error,
                                                   &std::cerr);
            }
          } else {
            PrintTranscriptText(transcript);
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
    return cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kOperationFailed);
  }
  return 0;
}
