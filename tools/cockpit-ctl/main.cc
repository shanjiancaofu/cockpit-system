#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "audio.grpc.pb.h"
#include "camera.grpc.pb.h"
#include "cockpit/core/config/system_config.h"
#include "cockpit/core/health/service_health.h"
#include "cockpit/core/json/json.h"
#include "cockpit/core/runtime/Args.h"
#include "common.pb.h"
#include "gateway.grpc.pb.h"
#include "recording.grpc.pb.h"
#include "tools/diagnostics/cli_output.h"
#include "voice.grpc.pb.h"

namespace cockpit {
namespace ctl {

constexpr int kDefaultWatchIntervalSec = 2;

namespace {

constexpr int kRpcTimeoutMs = 700;
constexpr std::uint64_t kCameraStaleTimeoutMs = 2000;

std::atomic<bool> g_stop{false};

void SignalHandler(int /*signum*/) {
  g_stop.store(true);
}

void SetupSignalHandler() {
  static_cast<void>(std::signal(SIGINT, SignalHandler));
  static_cast<void>(std::signal(SIGTERM, SignalHandler));
}

void ClearScreen() {
  std::cout << "\033[2J\033[H" << std::flush;
}

void PrintUsage() {
  std::cout << "Usage:\n"
            << "  cockpit-ctl status [--config configs/config.yaml]\n"
            << "  cockpit-ctl status --watch [--interval SEC] [--config configs/config.yaml]\n"
            << "  cockpit-ctl health [--config configs/config.yaml]\n"
            << "  cockpit-ctl dependencies [--config configs/config.yaml]\n"
            << "\nOptions:\n"
            << "  --config PATH    config file path (default: configs/config.yaml)\n"
            << "  --watch          watch mode, refresh status periodically\n"
            << "  --output FORMAT  text or json (default: text)\n"
            << "  --interval SEC   refresh interval in seconds (default: "
            << kDefaultWatchIntervalSec << ")\n";
}

void SetContext(grpc::ClientContext* context) {
  context->set_deadline(std::chrono::system_clock::now() +
                        std::chrono::milliseconds(kRpcTimeoutMs));
}

std::string RpcError(const grpc::Status& status) {
  if (status.error_message().empty()) {
    return status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED ? "deadline exceeded"
                                                                      : "rpc failed";
  }
  return status.error_message();
}

const char* CaptureStateName(proto::audio::CaptureState state) {
  switch (state) {
    case proto::audio::CAPTURE_STATE_STOPPED:
      return "stopped";
    case proto::audio::CAPTURE_STATE_STARTING:
      return "starting";
    case proto::audio::CAPTURE_STATE_RUNNING:
      return "running";
    case proto::audio::CAPTURE_STATE_RECOVERING:
      return "recovering";
    case proto::audio::CAPTURE_STATE_FAULTED:
      return "faulted";
    case proto::audio::CAPTURE_STATE_UNSPECIFIED:
    default:
      return "unspecified";
  }
}

const char* VoiceActivityName(proto::audio::VoiceActivityState state) {
  switch (state) {
    case proto::audio::VOICE_ACTIVITY_STATE_DISABLED:
      return "disabled";
    case proto::audio::VOICE_ACTIVITY_STATE_SILENCE:
      return "silence";
    case proto::audio::VOICE_ACTIVITY_STATE_SPEECH:
      return "speech";
    case proto::audio::VOICE_ACTIVITY_STATE_UNSPECIFIED:
    default:
      return "unspecified";
  }
}

const char* CameraStateName(proto::camera::CameraPreviewState state) {
  switch (state) {
    case proto::camera::CAMERA_PREVIEW_STATE_STOPPED:
      return "stopped";
    case proto::camera::CAMERA_PREVIEW_STATE_RUNNING:
      return "running";
    case proto::camera::CAMERA_PREVIEW_STATE_RECOVERING:
      return "recovering";
    case proto::camera::CAMERA_PREVIEW_STATE_FAULTED:
      return "faulted";
    case proto::camera::CAMERA_PREVIEW_STATE_UNSPECIFIED:
    default:
      return "unspecified";
  }
}

std::uint64_t NowMs() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count());
}

const char* CameraFrameHealth(const proto::camera::CameraStatus& camera, std::uint64_t now_ms) {
  if (camera.state() != proto::camera::CAMERA_PREVIEW_STATE_RUNNING) {
    return "inactive";
  }
  if (camera.frames_received() == 0 || camera.last_frame_received_at_ms() == 0) {
    return "waiting";
  }
  const std::uint64_t age_ms = now_ms >= camera.last_frame_received_at_ms()
                                   ? now_ms - camera.last_frame_received_at_ms()
                                   : 0;
  return age_ms <= kCameraStaleTimeoutMs ? "live" : "stalled";
}

const char* InteractionStateName(proto::voice::InteractionState state) {
  switch (state) {
    case proto::voice::INTERACTION_STATE_DISABLED:
      return "disabled";
    case proto::voice::INTERACTION_STATE_LISTENING:
      return "listening";
    case proto::voice::INTERACTION_STATE_PROCESSING:
      return "processing";
    case proto::voice::INTERACTION_STATE_FAULTED:
      return "faulted";
    case proto::voice::INTERACTION_STATE_UNSPECIFIED:
    default:
      return "unspecified";
  }
}

const char* RuntimeModuleStateName(proto::common::RuntimeModuleState state) {
  switch (state) {
    case proto::common::RUNTIME_MODULE_STATE_CREATED:
      return "created";
    case proto::common::RUNTIME_MODULE_STATE_STARTING:
      return "starting";
    case proto::common::RUNTIME_MODULE_STATE_RUNNING:
      return "running";
    case proto::common::RUNTIME_MODULE_STATE_STOPPING:
      return "stopping";
    case proto::common::RUNTIME_MODULE_STATE_STOPPED:
      return "stopped";
    case proto::common::RUNTIME_MODULE_STATE_FAILED:
      return "failed";
    case proto::common::RUNTIME_MODULE_STATE_UNSPECIFIED:
    default:
      return "unspecified";
  }
}

const char* ServiceHealthStateName(proto::common::ServiceHealthState state) {
  return health::StateName(state);
}

bool CheckHealth(const proto::common::ServiceHealth& value, const char* service_name,
                 std::string* error) {
  if (health::PassesHealthCheck(value.state())) {
    return true;
  }
  *error = value.message().empty()
               ? std::string(service_name) + " health is " + ServiceHealthStateName(value.state())
               : value.message();
  return false;
}

void PrintHealth(const proto::common::ServiceHealth& health) {
  std::cout << "  health: " << ServiceHealthStateName(health.state());
  if (!health.message().empty()) {
    std::cout << " message=\"" << health.message() << "\"";
  }
  std::cout << "\n";
}

const char* RecordingStateName(proto::recording::RecordingState state) {
  switch (state) {
    case proto::recording::RECORDING_STATE_IDLE:
      return "idle";
    case proto::recording::RECORDING_STATE_RECORDING:
      return "recording";
    case proto::recording::RECORDING_STATE_FAULTED:
      return "faulted";
    case proto::recording::RECORDING_STATE_UNSPECIFIED:
    default:
      return "unspecified";
  }
}

template <typename RepeatedModules>
void PrintModules(const RepeatedModules& modules) {
  for (const auto& module : modules) {
    std::cout << "  module: " << module.name() << '=' << RuntimeModuleStateName(module.state())
              << "\n";
  }
}

void PrintServiceHeader(const std::string& name, const std::string& address) {
  std::cout << name << " (" << address << ")\n";
}

void PrintUnavailable(const grpc::Status& status) {
  std::cout << "  available: no\n"
            << "  error: " << RpcError(status) << "\n";
}

void PrintGatewayStatus(const std::string& address) {
  PrintServiceHeader("gateway", address);
  auto stub = proto::gateway::CockpitGateway::NewStub(
      grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));
  proto::common::Empty request;
  proto::gateway::GatewayStatus gateway;
  grpc::ClientContext context;
  SetContext(&context);
  const grpc::Status status = stub->GetStatus(&context, request, &gateway);
  if (!status.ok()) {
    PrintUnavailable(status);
    return;
  }
  std::cout << "  available: yes\n"
            << "  vehicle_state: "
            << (gateway.vehicle_state_available() ? "available" : "unavailable")
            << " age_ms=" << gateway.last_vehicle_state_age_ms() << "\n"
            << "  events_published: " << gateway.events_published() << "\n";
  PrintHealth(gateway.health());
}

void PrintAudioStatus(const std::string& address) {
  PrintServiceHeader("audio", address);
  auto stub = proto::audio::AudioControl::NewStub(
      grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));
  proto::common::Empty request;
  proto::audio::AudioStatus audio;
  grpc::ClientContext context;
  SetContext(&context);
  const grpc::Status status = stub->GetStatus(&context, request, &audio);
  if (!status.ok()) {
    PrintUnavailable(status);
    return;
  }
  std::cout << "  available: yes\n"
            << "  capture: " << CaptureStateName(audio.capture_state())
            << " device=" << audio.input_device() << " format=" << audio.sample_rate_hz() << "Hz/"
            << audio.channels() << "ch/" << audio.frame_ms() << "ms\n"
            << "  vad: " << VoiceActivityName(audio.voice_activity_state())
            << " level=" << audio.input_level_dbfs()
            << " dBFS frames=" << audio.metrics().vad_frames_processed() << "\n"
            << "  asr: " << (audio.asr_enabled() ? "enabled" : "disabled")
            << " transcripts=" << audio.metrics().transcripts_published() << "\n";
  PrintHealth(audio.health());
  PrintModules(audio.modules());
  if (!audio.last_error().empty()) {
    std::cout << "  last_error: " << audio.last_error() << "\n";
  }
}

void PrintVoiceStatus(const std::string& address) {
  PrintServiceHeader("voice", address);
  auto stub = proto::voice::VoiceInteractionControl::NewStub(
      grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));
  proto::common::Empty request;
  proto::voice::VoiceInteractionStatus voice;
  grpc::ClientContext context;
  SetContext(&context);
  const grpc::Status status = stub->GetStatus(&context, request, &voice);
  if (!status.ok()) {
    PrintUnavailable(status);
    return;
  }
  std::cout << "  available: yes\n"
            << "  state: " << InteractionStateName(voice.state()) << "\n"
            << "  transcripts: " << voice.metrics().transcripts_received()
            << " responses=" << voice.metrics().responses_published()
            << " actions=" << voice.metrics().actions_succeeded() << "/"
            << voice.metrics().actions_attempted() << "\n";
  PrintHealth(voice.health());
  if (!voice.latest_response().response_text().empty()) {
    std::cout << "  latest: " << voice.latest_response().response_text() << "\n";
  }
  if (!voice.last_error().empty()) {
    std::cout << "  last_error: " << voice.last_error() << "\n";
  }
}

void PrintCameraStatus(const std::string& address) {
  PrintServiceHeader("camera", address);
  auto stub = proto::camera::CameraControl::NewStub(
      grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));
  proto::common::Empty request;
  proto::camera::CameraStatus camera;
  grpc::ClientContext context;
  SetContext(&context);
  const grpc::Status status = stub->GetStatus(&context, request, &camera);
  if (!status.ok()) {
    PrintUnavailable(status);
    return;
  }
  const std::uint64_t now_ms = NowMs();
  const std::uint64_t frame_age_ms =
      camera.last_frame_received_at_ms() > 0 && now_ms >= camera.last_frame_received_at_ms()
          ? now_ms - camera.last_frame_received_at_ms()
          : 0;
  std::cout << "  available: yes\n"
            << "  preview: " << CameraStateName(camera.state()) << " device=" << camera.device()
            << " size=" << camera.width() << "x" << camera.height() << "@" << camera.fps() << "\n"
            << "  frames: received=" << camera.frames_received()
            << " dropped=" << camera.frames_dropped()
            << " source_skipped=" << camera.source_frames_skipped() << "\n"
            << "  frame_health: " << CameraFrameHealth(camera, now_ms) << " age_ms=" << frame_age_ms
            << " sequence=" << camera.last_frame_sequence() << "\n";
  std::cout << "  continuity: consecutive_drops=" << camera.consecutive_frame_drops()
            << " max_drops=" << camera.max_consecutive_frame_drops()
            << " consecutive_gaps=" << camera.consecutive_source_gaps()
            << " max_gaps=" << camera.max_consecutive_source_gaps() << "\n";
  std::cout << "  recovery: restarts=" << camera.restart_count()
            << " recovers=" << camera.recover_count()
            << " last_recover_at_ms=" << camera.last_recover_at_ms() << "\n";
  PrintHealth(camera.health());
  PrintModules(camera.modules());
  if (!camera.last_error_kind().empty()) {
    std::cout << "  last_error_kind: " << camera.last_error_kind() << "\n";
  }
  if (!camera.last_error().empty()) {
    std::cout << "  last_error: " << camera.last_error() << "\n";
  }
}

void PrintRecordingStatus(const std::string& address) {
  PrintServiceHeader("recording", address);
  auto stub = proto::recording::RecordingControl::NewStub(
      grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));
  proto::common::Empty request;
  proto::recording::RecordingStatus recording;
  grpc::ClientContext context;
  SetContext(&context);
  const grpc::Status status = stub->GetStatus(&context, request, &recording);
  if (!status.ok()) {
    PrintUnavailable(status);
    return;
  }
  std::cout << "  available: yes\n"
            << "  state: " << RecordingStateName(recording.state())
            << " session=" << recording.session_id() << " trigger=" << recording.trigger() << "\n"
            << "  messages: " << recording.messages_written()
            << " stored_sessions=" << recording.stored_sessions()
            << " stored_bytes=" << recording.stored_bytes() << "\n";
  PrintHealth(recording.health());
  if (!recording.last_error().empty()) {
    std::cout << "  last_error: " << recording.last_error() << "\n";
  }
}

bool CheckGateway(const std::string& address, std::string* error) {
  auto stub = proto::gateway::CockpitGateway::NewStub(
      grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));
  proto::common::Empty request;
  proto::gateway::GatewayStatus gateway;
  grpc::ClientContext context;
  SetContext(&context);
  const grpc::Status status = stub->GetStatus(&context, request, &gateway);
  if (!status.ok()) {
    *error = RpcError(status);
    return false;
  }
  return CheckHealth(gateway.health(), "gateway", error);
}

bool CheckAudio(const std::string& address, std::string* error) {
  auto stub = proto::audio::AudioControl::NewStub(
      grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));
  proto::common::Empty request;
  proto::audio::AudioStatus audio;
  grpc::ClientContext context;
  SetContext(&context);
  const grpc::Status status = stub->GetStatus(&context, request, &audio);
  if (!status.ok()) {
    *error = RpcError(status);
    return false;
  }
  return CheckHealth(audio.health(), "audio", error);
}

bool CheckVoice(const std::string& address, std::string* error) {
  auto stub = proto::voice::VoiceInteractionControl::NewStub(
      grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));
  proto::common::Empty request;
  proto::voice::VoiceInteractionStatus voice;
  grpc::ClientContext context;
  SetContext(&context);
  const grpc::Status status = stub->GetStatus(&context, request, &voice);
  if (!status.ok()) {
    *error = RpcError(status);
    return false;
  }
  return CheckHealth(voice.health(), "voice", error);
}

bool CheckCamera(const std::string& address, std::string* error) {
  auto stub = proto::camera::CameraControl::NewStub(
      grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));
  proto::common::Empty request;
  proto::camera::CameraStatus camera;
  grpc::ClientContext context;
  SetContext(&context);
  const grpc::Status status = stub->GetStatus(&context, request, &camera);
  if (!status.ok()) {
    *error = RpcError(status);
    return false;
  }
  return CheckHealth(camera.health(), "camera", error);
}

bool CheckRecording(const std::string& address, std::string* error) {
  auto stub = proto::recording::RecordingControl::NewStub(
      grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));
  proto::common::Empty request;
  proto::recording::RecordingStatus recording;
  grpc::ClientContext context;
  SetContext(&context);
  const grpc::Status status = stub->GetStatus(&context, request, &recording);
  if (!status.ok()) {
    *error = RpcError(status);
    return false;
  }
  return CheckHealth(recording.health(), "recording", error);
}

using HealthCheck = bool (*)(const std::string&, std::string*);

bool RunOneHealthCheck(const std::string& name, const std::string& address, HealthCheck check) {
  std::string error;
  const bool healthy = check(address, &error);
  std::cout << (healthy ? "ok   " : "fail ") << name << " " << address;
  if (!healthy) {
    std::cout << " error=\"" << error << "\"";
  }
  std::cout << "\n";
  return healthy;
}

int RunStatus(const config::SystemConfig& config) {
  std::cout << "cockpit-system status\n";
  std::cout << "system: " << config.system().name << " vehicle_id=" << config.system().vehicle_id
            << "\n\n";

  PrintGatewayStatus(config.services().gateway.grpc.listen_address);
  std::cout << '\n';
  PrintAudioStatus(config.services().audio.grpc.listen_address);
  std::cout << '\n';
  PrintVoiceStatus(config.services().voice_interaction.grpc.listen_address);
  std::cout << '\n';
  PrintCameraStatus(config.services().camera.grpc.listen_address);
  std::cout << '\n';
  PrintRecordingStatus(config.services().recording.grpc.listen_address);
  return 0;
}

template <typename Response, typename Fetch>
std::string FetchStatusJson(Fetch fetch) {
  Response response;
  grpc::ClientContext context;
  SetContext(&context);
  const grpc::Status status = fetch(&context, &response);
  if (!status.ok()) {
    return "{\"available\":false,\"error\":\"" + json::EscapeString(RpcError(status)) + "\"}";
  }
  std::string json_text;
  std::string error;
  if (!diagnostics::JsonString(response, &json_text, &error)) {
    return "{\"available\":true,\"serialization_error\":\"" + json::EscapeString(error) + "\"}";
  }
  return json_text;
}

int RunStatusJson(const config::SystemConfig& config) {
  auto gateway = proto::gateway::CockpitGateway::NewStub(grpc::CreateChannel(
      config.services().gateway.grpc.listen_address, grpc::InsecureChannelCredentials()));
  auto audio = proto::audio::AudioControl::NewStub(grpc::CreateChannel(
      config.services().audio.grpc.listen_address, grpc::InsecureChannelCredentials()));
  auto voice = proto::voice::VoiceInteractionControl::NewStub(grpc::CreateChannel(
      config.services().voice_interaction.grpc.listen_address, grpc::InsecureChannelCredentials()));
  auto camera = proto::camera::CameraControl::NewStub(grpc::CreateChannel(
      config.services().camera.grpc.listen_address, grpc::InsecureChannelCredentials()));
  auto recording = proto::recording::RecordingControl::NewStub(grpc::CreateChannel(
      config.services().recording.grpc.listen_address, grpc::InsecureChannelCredentials()));
  const proto::common::Empty request;

  std::cout << "{\"system\":{\"name\":\"" << json::EscapeString(config.system().name)
            << "\",\"vehicle_id\":\"" << json::EscapeString(config.system().vehicle_id)
            << "\"},\"services\":{";
  std::cout << "\"gateway\":"
            << FetchStatusJson<proto::gateway::GatewayStatus>(
                   [&](grpc::ClientContext* context, proto::gateway::GatewayStatus* response) {
                     return gateway->GetStatus(context, request, response);
                   });
  std::cout << ",\"audio\":"
            << FetchStatusJson<proto::audio::AudioStatus>(
                   [&](grpc::ClientContext* context, proto::audio::AudioStatus* response) {
                     return audio->GetStatus(context, request, response);
                   });
  std::cout << ",\"voice\":"
            << FetchStatusJson<proto::voice::VoiceInteractionStatus>(
                   [&](grpc::ClientContext* context,
                       proto::voice::VoiceInteractionStatus* response) {
                     return voice->GetStatus(context, request, response);
                   });
  std::cout << ",\"camera\":"
            << FetchStatusJson<proto::camera::CameraStatus>(
                   [&](grpc::ClientContext* context, proto::camera::CameraStatus* response) {
                     return camera->GetStatus(context, request, response);
                   });
  std::cout << ",\"recording\":"
            << FetchStatusJson<proto::recording::RecordingStatus>(
                   [&](grpc::ClientContext* context, proto::recording::RecordingStatus* response) {
                     return recording->GetStatus(context, request, response);
                   });
  std::cout << "}}\n";
  return diagnostics::ToInt(diagnostics::ExitCode::kSuccess);
}

int RunHealth(const config::SystemConfig& config) {
  bool healthy = true;
  std::cout << "cockpit-system health\n";
  healthy &=
      RunOneHealthCheck("gateway", config.services().gateway.grpc.listen_address, CheckGateway);
  healthy &= RunOneHealthCheck("audio", config.services().audio.grpc.listen_address, CheckAudio);
  healthy &= RunOneHealthCheck("voice", config.services().voice_interaction.grpc.listen_address,
                               CheckVoice);
  healthy &= RunOneHealthCheck("camera", config.services().camera.grpc.listen_address, CheckCamera);
  healthy &= RunOneHealthCheck("recording", config.services().recording.grpc.listen_address,
                               CheckRecording);
  return healthy ? diagnostics::ToInt(diagnostics::ExitCode::kSuccess)
                 : diagnostics::ToInt(diagnostics::ExitCode::kUnhealthy);
}

int RunHealthJson(const config::SystemConfig& config) {
  struct Target {
    const char* name;
    const std::string* address;
    HealthCheck check;
  };
  const std::vector<Target> targets = {
      {"gateway", &config.services().gateway.grpc.listen_address, CheckGateway},
      {"audio", &config.services().audio.grpc.listen_address, CheckAudio},
      {"voice", &config.services().voice_interaction.grpc.listen_address, CheckVoice},
      {"camera", &config.services().camera.grpc.listen_address, CheckCamera},
      {"recording", &config.services().recording.grpc.listen_address, CheckRecording},
  };
  bool healthy = true;
  std::cout << "{\"healthy\":";
  std::vector<std::string> results;
  for (const auto& target : targets) {
    std::string error;
    const bool current_healthy = target.check(*target.address, &error);
    healthy &= current_healthy;
    results.push_back("{\"name\":\"" + std::string(target.name) + "\",\"address\":\"" +
                      json::EscapeString(*target.address) +
                      "\",\"healthy\":" + (current_healthy ? "true" : "false") + ",\"error\":\"" +
                      json::EscapeString(error) + "\"}");
  }
  std::cout << (healthy ? "true" : "false") << ",\"services\":[";
  for (std::size_t index = 0; index < results.size(); ++index) {
    if (index > 0) {
      std::cout << ',';
    }
    std::cout << results[index];
  }
  std::cout << "]}\n";
  return healthy ? diagnostics::ToInt(diagnostics::ExitCode::kSuccess)
                 : diagnostics::ToInt(diagnostics::ExitCode::kUnhealthy);
}

void PrintStringList(const std::vector<std::string>& values) {
  if (values.empty()) {
    std::cout << "[]";
    return;
  }
  std::cout << '[';
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i > 0) {
      std::cout << ", ";
    }
    std::cout << values[i];
  }
  std::cout << ']';
}

int RunDependencies(const config::SystemConfig& config) {
  std::cout << "cockpit-system dependencies\n";
  for (const auto& dependency : config.runtime().dependencies) {
    std::cout << dependency.service << "\n  required: ";
    PrintStringList(dependency.required);
    std::cout << "\n  optional: ";
    PrintStringList(dependency.optional);
    std::cout << "\n";
  }
  return 0;
}

int WatchStatus(const config::SystemConfig& config, int interval_sec) {
  SetupSignalHandler();
  while (!g_stop.load()) {
    ClearScreen();
    const int rc = RunStatus(config);
    if (rc != 0) {
      return rc;
    }
    // 等待 interval 秒，每秒检查一次停止信号
    for (int i = 0; i < interval_sec && !g_stop.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
  std::cout << "\nstopped.\n";
  return 0;
}

}  // namespace
}  // namespace ctl
}  // namespace cockpit

int main(int argc, char** argv) {
  const auto args = cockpit::runtime::Args::Parse(argc, argv);
  const std::string command = argc > 1 ? argv[1] : "";
  if (command.empty() || command == "help" || command == "--help" || command == "-h") {
    cockpit::ctl::PrintUsage();
    return command.empty() ? 1 : 0;
  }
  if (command != "status" && command != "health" && command != "dependencies") {
    std::cerr << "unknown command: " << command << "\n";
    cockpit::ctl::PrintUsage();
    return cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kInvalidArguments);
  }

  const std::string config_path = args.GetString("config", "configs/config.yaml");
  const auto config = cockpit::config::SystemConfig::LoadFromFile(config_path);
  cockpit::diagnostics::OutputFormat output_format;
  std::string output_error;
  if (!cockpit::diagnostics::ParseOutputFormat(args.GetString("output", "text"), &output_format,
                                               &output_error)) {
    std::cerr << output_error << '\n';
    return cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kInvalidArguments);
  }

  if (command == "health") {
    return output_format == cockpit::diagnostics::OutputFormat::kJson
               ? cockpit::ctl::RunHealthJson(config)
               : cockpit::ctl::RunHealth(config);
  }
  if (command == "dependencies") {
    if (output_format == cockpit::diagnostics::OutputFormat::kJson) {
      cockpit::diagnostics::WriteJsonError("invalid_arguments",
                                           "dependencies does not support JSON output", &std::cerr);
      return cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kInvalidArguments);
    }
    return cockpit::ctl::RunDependencies(config);
  }

  if (args.HasFlag("watch")) {
    if (output_format == cockpit::diagnostics::OutputFormat::kJson) {
      cockpit::diagnostics::WriteJsonError("invalid_arguments",
                                           "watch does not support JSON output", &std::cerr);
      return cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kInvalidArguments);
    }
    const int interval_sec = args.GetInt("interval", cockpit::ctl::kDefaultWatchIntervalSec);
    if (interval_sec < 1) {
      std::cerr << "interval must be >= 1 second\n";
      return cockpit::diagnostics::ToInt(cockpit::diagnostics::ExitCode::kInvalidArguments);
    }
    return cockpit::ctl::WatchStatus(config, interval_sec);
  }
  return output_format == cockpit::diagnostics::OutputFormat::kJson
             ? cockpit::ctl::RunStatusJson(config)
             : cockpit::ctl::RunStatus(config);
}
