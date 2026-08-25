#include "cockpit/library/bridge/bridge_grpc_service.h"

#include <grpcpp/grpcpp.h>
#include <unistd.h>

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "bridge.grpc.pb.h"
#include "cockpit/modules/bridge/fake_bridge_provider.h"
#include "common.pb.h"

int main() {
  const std::filesystem::path socket = "/tmp/cockpit-bridge-" + std::to_string(getpid()) + ".sock";
  std::error_code filesystem_error;
  std::filesystem::remove(socket, filesystem_error);
  cockpit::bridge::BridgeService service(
      cockpit::bridge::CreateFakeBridgeProvider(cockpit::bridge::FakeBridgeOutcome::kSucceeded),
      1000);
  cockpit::bridge::BridgeGrpcService grpc_service(service);
  const std::string address = "unix:" + socket.string();
  if (!grpc_service.Start(address)) return 1;
  auto stub = cockpit::proto::bridge::BridgeControl::NewStub(
      grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));

  cockpit::proto::bridge::SubmitBridgeGoalRequest request;
  request.set_goal_id("grpc-goal");
  request.mutable_target()->set_x_m(3.0);
  request.mutable_target()->set_y_m(4.0);
  request.mutable_target()->set_yaw_rad(0.25);
  request.mutable_target()->set_frame_id("map");
  cockpit::proto::bridge::BridgeStatus response;
  grpc::ClientContext submit_context;
  if (!stub->SubmitGoal(&submit_context, request, &response).ok() ||
      response.state() != cockpit::proto::bridge::BRIDGE_STATE_ACCEPTED) {
    std::cerr << "bridge gRPC submit failed\n";
    return 1;
  }

  cockpit::proto::common::Empty empty;
  grpc::ClientContext executing_context;
  if (!stub->GetStatus(&executing_context, empty, &response).ok() ||
      response.state() != cockpit::proto::bridge::BRIDGE_STATE_EXECUTING) {
    std::cerr << "bridge gRPC executing state missing\n";
    return 1;
  }
  grpc::ClientContext succeeded_context;
  if (!stub->GetStatus(&succeeded_context, empty, &response).ok() ||
      response.state() != cockpit::proto::bridge::BRIDGE_STATE_SUCCEEDED ||
      response.current_pose().x_m() != 3.0 ||
      response.health().state() != cockpit::proto::common::SERVICE_HEALTH_STATE_OK) {
    std::cerr << "bridge gRPC terminal status is invalid\n";
    return 1;
  }

  grpc_service.Shutdown();
  std::filesystem::remove(socket, filesystem_error);
  std::cout << "bridge gRPC service tests passed\n";
  return 0;
}
