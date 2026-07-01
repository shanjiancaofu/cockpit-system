#include <grpcpp/grpcpp.h>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

#include "audio.grpc.pb.h"
#include "camera.grpc.pb.h"
#include "common.pb.h"
#include "core/config/system_config.h"
#include "core/runtime/Args.h"
#include "gateway.grpc.pb.h"
#include "voice.grpc.pb.h"

namespace cockpit {
namespace ctl {
namespace {

constexpr int kRpcTimeoutMs = 700;

void PrintUsage() {
  std::cout << "Usage:\n"
            << "  cockpit-ctl status [--config configs/config.yaml]\n";
}

void SetContext(grpc::ClientContext* context) {
  context->set_wait_for_ready(false);
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
    case proto::camera::CAMERA_PREVIEW_STATE_FAULTED:
      return "faulted";
    case proto::camera::CAMERA_PREVIEW_STATE_UNSPECIFIED:
    default:
      return "unspecified";
  }
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
  proto::vehicle::VehicleState state;
  grpc::ClientContext context;
  SetContext(&context);
  const grpc::Status status = stub->GetLatestVehicleState(&context, request, &state);
  if (!status.ok()) {
    PrintUnavailable(status);
    return;
  }
  std::cout << "  available: yes\n"
            << "  vehicle: speed=" << state.speed_kph() << " kph gear=" << state.gear()
            << " soc=" << state.soc_percent() << "% source=" << state.source() << "\n";
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
  std::cout << "  available: yes\n"
            << "  preview: " << CameraStateName(camera.state()) << " device=" << camera.device()
            << " size=" << camera.width() << "x" << camera.height() << "@" << camera.fps() << "\n"
            << "  frames: received=" << camera.frames_received()
            << " dropped=" << camera.frames_dropped() << "\n";
  if (!camera.last_error().empty()) {
    std::cout << "  last_error: " << camera.last_error() << "\n";
  }
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
  if (command != "status") {
    std::cerr << "unknown command: " << command << "\n";
    cockpit::ctl::PrintUsage();
    return 1;
  }

  const std::string config_path = args.GetString("config", "configs/config.yaml");
  const auto config = cockpit::config::SystemConfig::LoadFromFile(config_path);
  return cockpit::ctl::RunStatus(config);
}
