#include "health_command.h"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

#include "audio.grpc.pb.h"
#include "camera.grpc.pb.h"
#include "cockpit/core/health/service_health.h"
#include "cockpit/core/json/json.h"
#include "common.pb.h"
#include "gateway.grpc.pb.h"
#include "recording.grpc.pb.h"
#include "sentinel.grpc.pb.h"
#include "vehicle_state.grpc.pb.h"
#include "voice.grpc.pb.h"

namespace cockpit {
namespace ctl {
namespace health {
namespace {

constexpr int kRpcTimeoutMs = 700;

void SetContext(grpc::ClientContext* context) {
  context->set_deadline(std::chrono::system_clock::now() +
                        std::chrono::milliseconds(kRpcTimeoutMs));
}

std::string RpcError(const grpc::Status& status) {
  if (!status.error_message().empty()) {
    return status.error_message();
  }
  return status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED ? "deadline exceeded"
                                                                    : "rpc failed";
}

bool CheckHealth(const proto::common::ServiceHealth& value, const char* service_name,
                 std::string* error) {
  if (cockpit::health::PassesHealthCheck(value.state())) {
    return true;
  }
  *error = value.message().empty() ? std::string(service_name) + " health is " +
                                         cockpit::health::StateName(value.state())
                                   : value.message();
  return false;
}

bool CheckGateway(const std::string& address, std::string* error) {
  auto stub = proto::gateway::CockpitGateway::NewStub(
      grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));
  proto::common::Empty request;
  proto::gateway::GatewayStatus response;
  grpc::ClientContext context;
  SetContext(&context);
  const grpc::Status status = stub->GetStatus(&context, request, &response);
  if (!status.ok()) {
    *error = RpcError(status);
    return false;
  }
  return CheckHealth(response.health(), "gateway", error);
}

bool CheckAudio(const std::string& address, std::string* error) {
  auto stub = proto::audio::AudioControl::NewStub(
      grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));
  proto::common::Empty request;
  proto::audio::AudioStatus response;
  grpc::ClientContext context;
  SetContext(&context);
  const grpc::Status status = stub->GetStatus(&context, request, &response);
  if (!status.ok()) {
    *error = RpcError(status);
    return false;
  }
  return CheckHealth(response.health(), "audio", error);
}

bool CheckVehicle(const std::string& address, std::string* error) {
  auto stub = proto::vehicle::VehicleDataService::NewStub(
      grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));
  proto::common::Empty request;
  proto::vehicle::CanLinkStatus response;
  grpc::ClientContext context;
  SetContext(&context);
  const grpc::Status status = stub->GetStatus(&context, request, &response);
  if (!status.ok()) {
    *error = RpcError(status);
    return false;
  }
  return CheckHealth(response.health(), "vehicle", error);
}

bool CheckVoice(const std::string& address, std::string* error) {
  auto stub = proto::voice::VoiceInteractionControl::NewStub(
      grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));
  proto::common::Empty request;
  proto::voice::VoiceInteractionStatus response;
  grpc::ClientContext context;
  SetContext(&context);
  const grpc::Status status = stub->GetStatus(&context, request, &response);
  if (!status.ok()) {
    *error = RpcError(status);
    return false;
  }
  return CheckHealth(response.health(), "voice", error);
}

bool CheckCamera(const std::string& address, std::string* error) {
  auto stub = proto::camera::CameraControl::NewStub(
      grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));
  proto::common::Empty request;
  proto::camera::CameraStatus response;
  grpc::ClientContext context;
  SetContext(&context);
  const grpc::Status status = stub->GetStatus(&context, request, &response);
  if (!status.ok()) {
    *error = RpcError(status);
    return false;
  }
  return CheckHealth(response.health(), "camera", error);
}

bool CheckRecording(const std::string& address, std::string* error) {
  auto stub = proto::recording::RecordingControl::NewStub(
      grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));
  proto::common::Empty request;
  proto::recording::RecordingStatus response;
  grpc::ClientContext context;
  SetContext(&context);
  const grpc::Status status = stub->GetStatus(&context, request, &response);
  if (!status.ok()) {
    *error = RpcError(status);
    return false;
  }
  return CheckHealth(response.health(), "recording", error);
}

bool CheckSentinel(const std::string& address, std::string* error) {
  auto stub = proto::sentinel::SentinelControl::NewStub(
      grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));
  proto::common::Empty request;
  proto::sentinel::SentinelStatus response;
  grpc::ClientContext context;
  SetContext(&context);
  const grpc::Status status = stub->GetStatus(&context, request, &response);
  if (!status.ok()) {
    *error = RpcError(status);
    return false;
  }
  return CheckHealth(response.health(), "sentinel", error);
}

using HealthCheck = bool (*)(const std::string&, std::string*);

struct Target {
  const char* name;
  const std::string* address;
  HealthCheck check;
};

}  // namespace

int Run(const config::SystemConfig& config, diagnostics::OutputFormat output_format,
        const std::string& mode) {
  std::vector<Target> targets = {
      {"gateway", &config.services().gateway.grpc.listen_address, CheckGateway},
      {"vehicle", &config.services().vehicle_data.grpc.listen_address, CheckVehicle},
  };
  if (mode == "normal" || mode == "development" || mode == "ui") {
    targets.push_back({"audio", &config.services().audio.grpc.listen_address, CheckAudio});
    targets.push_back(
        {"voice", &config.services().voice_interaction.grpc.listen_address, CheckVoice});
    targets.push_back({"camera", &config.services().camera.grpc.listen_address, CheckCamera});
    targets.push_back(
        {"recording", &config.services().recording.grpc.listen_address, CheckRecording});
    targets.push_back({"sentinel", &config.services().sentinel.grpc.listen_address, CheckSentinel});
  }
  bool healthy = true;
  if (output_format == diagnostics::OutputFormat::kText) {
    std::cout << "cockpit-system health\n";
    for (const auto& target : targets) {
      std::string error;
      const bool current_healthy = target.check(*target.address, &error);
      healthy &= current_healthy;
      std::cout << (current_healthy ? "ok   " : "fail ") << target.name << ' ' << *target.address;
      if (!current_healthy) {
        std::cout << " error=\"" << error << '"';
      }
      std::cout << '\n';
    }
  } else {
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
    std::cout << "{\"healthy\":" << (healthy ? "true" : "false") << ",\"services\":[";
    for (std::size_t index = 0; index < results.size(); ++index) {
      if (index > 0) {
        std::cout << ',';
      }
      std::cout << results[index];
    }
    std::cout << "]}\n";
  }
  return healthy ? diagnostics::ToInt(diagnostics::ExitCode::kSuccess)
                 : diagnostics::ToInt(diagnostics::ExitCode::kUnhealthy);
}

}  // namespace health
}  // namespace ctl
}  // namespace cockpit
