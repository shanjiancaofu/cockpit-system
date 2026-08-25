#include <grpcpp/grpcpp.h>

#include <chrono>
#include <cmath>
#include <iostream>
#include <string>

#include "bridge.grpc.pb.h"
#include "cockpit/core/runtime/process_runtime.h"
#include "common.pb.h"
#include "tools/diagnostics/cli_output.h"

namespace {

void Usage() {
  std::cerr << "usage: bridge-ctl --status|--submit|--cancel [--goal-id ID] "
               "[--x M --y M --yaw RAD] [--output text|json] [--config PATH]\n";
}

bool ParseDouble(const std::string& value, double* result) {
  try {
    std::size_t consumed = 0;
    const double parsed = std::stod(value, &consumed);
    if (consumed != value.size() || !std::isfinite(parsed)) return false;
    *result = parsed;
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

void Deadline(grpc::ClientContext* context) {
  context->set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(1));
}

void PrintText(const cockpit::proto::bridge::NavigationStatus& status) {
  std::cout << "state: " << cockpit::proto::bridge::NavigationState_Name(status.state()) << '\n'
            << "goal id: " << status.goal_id() << '\n'
            << "target: " << status.target().x_m() << ',' << status.target().y_m() << ','
            << status.target().yaw_rad() << " frame=" << status.target().frame_id() << '\n'
            << "message: " << status.message() << '\n'
            << "last error: " << status.last_error() << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  const auto runtime = cockpit::runtime::ProcessRuntime::Create(argc, argv, "bridge-ctl");
  const bool status_command = runtime.args().HasFlag("status");
  const bool submit_command = runtime.args().HasFlag("submit");
  const bool cancel_command = runtime.args().HasFlag("cancel");
  if (static_cast<int>(status_command) + static_cast<int>(submit_command) +
          static_cast<int>(cancel_command) !=
      1) {
    Usage();
    return 2;
  }
  cockpit::diagnostics::OutputFormat output_format;
  std::string error;
  if (!cockpit::diagnostics::ParseOutputFormat(runtime.args().GetString("output", "text"),
                                               &output_format, &error)) {
    std::cerr << error << '\n';
    return 2;
  }
  const std::string address =
      runtime.args().GetString("address", runtime.config().services().bridge.grpc.listen_address);
  auto stub = cockpit::proto::bridge::BridgeControl::NewStub(
      grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));
  cockpit::proto::bridge::NavigationStatus response;
  grpc::Status rpc_status;
  grpc::ClientContext context;
  Deadline(&context);
  if (status_command) {
    cockpit::proto::common::Empty request;
    rpc_status = stub->GetNavigationStatus(&context, request, &response);
  } else if (cancel_command) {
    cockpit::proto::bridge::CancelNavigationGoalRequest request;
    request.set_goal_id(runtime.args().GetString("goal-id", ""));
    if (request.goal_id().empty()) {
      Usage();
      return 2;
    }
    rpc_status = stub->CancelNavigationGoal(&context, request, &response);
  } else {
    double x_m = 0.0;
    double y_m = 0.0;
    double yaw_rad = 0.0;
    const std::string goal_id = runtime.args().GetString("goal-id", "");
    if (goal_id.empty() || !ParseDouble(runtime.args().GetString("x", ""), &x_m) ||
        !ParseDouble(runtime.args().GetString("y", ""), &y_m) ||
        !ParseDouble(runtime.args().GetString("yaw", ""), &yaw_rad)) {
      Usage();
      return 2;
    }
    cockpit::proto::bridge::SubmitNavigationGoalRequest request;
    request.set_goal_id(goal_id);
    request.mutable_target()->set_x_m(x_m);
    request.mutable_target()->set_y_m(y_m);
    request.mutable_target()->set_yaw_rad(yaw_rad);
    request.mutable_target()->set_frame_id("map");
    rpc_status = stub->SubmitNavigationGoal(&context, request, &response);
  }
  if (!rpc_status.ok()) {
    std::cerr << (rpc_status.error_message().empty() ? "bridge RPC failed"
                                                     : rpc_status.error_message())
              << '\n';
    return 1;
  }
  if (output_format == cockpit::diagnostics::OutputFormat::kJson) {
    if (!cockpit::diagnostics::WriteJson(response, &std::cout, &error)) {
      std::cerr << error << '\n';
      return 1;
    }
  } else {
    PrintText(response);
  }
  runtime.MarkStopped();
  return 0;
}
