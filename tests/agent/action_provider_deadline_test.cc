#include <grpcpp/grpcpp.h>
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "agent/hmi/local_hmi_command_provider.h"
#include "agent/vehicle/gateway_vehicle_status_client.h"
#include "gateway.grpc.pb.h"
#include "hmi.grpc.pb.h"

namespace {

class BlockingCall {
 public:
  void EnterAndWait() {
    std::unique_lock<std::mutex> lock(mutex_);
    entered_ = true;
    changed_.notify_all();
    changed_.wait(lock, [this] {
      return released_;
    });
  }

  bool WaitUntilEntered() {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_until(lock, std::chrono::system_clock::now() + std::chrono::seconds(1),
                               [this] {
                                 return entered_;
                               });
  }

  void Release() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      released_ = true;
    }
    changed_.notify_all();
  }

 private:
  std::mutex mutex_;
  std::condition_variable changed_;
  bool entered_ = false;
  bool released_ = false;
};

class BlockingHmiService final : public cockpit::proto::hmi::HmiControl::Service {
 public:
  grpc::Status Execute(grpc::ServerContext*, const cockpit::proto::hmi::ExecuteHmiCommandRequest*,
                       cockpit::proto::hmi::ExecuteHmiCommandResponse*) override {
    call_.EnterAndWait();
    return grpc::Status::OK;
  }

  BlockingCall& call() {
    return call_;
  }

 private:
  BlockingCall call_;
};

class BlockingGatewayService final : public cockpit::proto::gateway::CockpitGateway::Service {
 public:
  grpc::Status GetLatestVehicleState(grpc::ServerContext*, const cockpit::proto::common::Empty*,
                                     cockpit::proto::vehicle::VehicleState*) override {
    call_.EnterAndWait();
    return grpc::Status::OK;
  }

  BlockingCall& call() {
    return call_;
  }

 private:
  BlockingCall call_;
};

std::unique_ptr<grpc::Server> StartServer(const std::string& address, grpc::Service* service) {
  grpc::ServerBuilder builder;
  builder.AddListeningPort(address, grpc::InsecureServerCredentials());
  builder.RegisterService(service);
  return builder.BuildAndStart();
}

}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("cockpit-action-provider-test-" + std::to_string(getpid()));
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  BlockingGatewayService gateway_service;
  const std::string gateway_address = "unix:" + (root / "gateway.sock").string();
  auto gateway_server = StartServer(gateway_address, &gateway_service);
  if (gateway_server == nullptr) {
    std::cerr << "failed to start blocking gateway server\n";
    return 1;
  }
  cockpit::voice::GatewayVehicleStatusClient vehicle_provider(gateway_address);
  cockpit::voice::VehicleStatusSnapshot snapshot;
  std::string vehicle_error;
  const auto vehicle_started = std::chrono::steady_clock::now();
  const cockpit::voice::ActionExecutionContext vehicle_context{
      vehicle_started + std::chrono::milliseconds(40),
      std::make_shared<cockpit::voice::ActionCancellation>()};
  const bool vehicle_succeeded =
      vehicle_provider.GetLatest(vehicle_context, &snapshot, &vehicle_error);
  const auto vehicle_elapsed = std::chrono::steady_clock::now() - vehicle_started;
  gateway_service.call().Release();
  gateway_server->Shutdown();
  gateway_server->Wait();
  if (vehicle_succeeded || vehicle_elapsed > std::chrono::milliseconds(500) ||
      vehicle_error.find("deadline exceeded") == std::string::npos) {
    std::cerr << "vehicle provider exceeded the caller's total deadline budget: " << vehicle_error
              << '\n';
    return 1;
  }

  BlockingHmiService hmi_deadline_service;
  const std::string hmi_deadline_address = "unix:" + (root / "hmi-deadline.sock").string();
  auto hmi_deadline_server = StartServer(hmi_deadline_address, &hmi_deadline_service);
  if (hmi_deadline_server == nullptr) {
    std::cerr << "failed to start HMI deadline server\n";
    return 1;
  }
  cockpit::voice::LocalHmiCommandProvider hmi_deadline_provider(hmi_deadline_address);
  std::string hmi_response;
  std::string hmi_error;
  const auto hmi_started = std::chrono::steady_clock::now();
  const cockpit::voice::ActionExecutionContext hmi_context{
      hmi_started + std::chrono::milliseconds(40),
      std::make_shared<cockpit::voice::ActionCancellation>()};
  const bool hmi_succeeded = hmi_deadline_provider.SendCommand(
      cockpit::voice::HmiCommand::kOpenCameraPreview, hmi_context, &hmi_response, &hmi_error);
  const auto hmi_elapsed = std::chrono::steady_clock::now() - hmi_started;
  hmi_deadline_service.call().Release();
  hmi_deadline_server->Shutdown();
  hmi_deadline_server->Wait();
  if (hmi_succeeded || hmi_elapsed > std::chrono::milliseconds(500)) {
    std::cerr << "HMI provider exceeded the caller's total deadline budget\n";
    return 1;
  }

  BlockingHmiService hmi_cancel_service;
  const std::string hmi_cancel_address = "unix:" + (root / "hmi-cancel.sock").string();
  auto hmi_cancel_server = StartServer(hmi_cancel_address, &hmi_cancel_service);
  if (hmi_cancel_server == nullptr) {
    std::cerr << "failed to start HMI cancellation server\n";
    return 1;
  }
  cockpit::voice::LocalHmiCommandProvider hmi_cancel_provider(hmi_cancel_address);
  bool cancelled_succeeded = true;
  std::string cancelled_error;
  std::thread hmi_request([&] {
    std::string response;
    const cockpit::voice::ActionExecutionContext action_context{
        std::chrono::steady_clock::now() + std::chrono::seconds(5),
        std::make_shared<cockpit::voice::ActionCancellation>()};
    cancelled_succeeded =
        hmi_cancel_provider.SendCommand(cockpit::voice::HmiCommand::kOpenCameraPreview,
                                        action_context, &response, &cancelled_error);
  });
  if (!hmi_cancel_service.call().WaitUntilEntered()) {
    std::cerr << "blocking HMI request did not enter the provider\n";
    hmi_cancel_service.call().Release();
    hmi_request.join();
    return 1;
  }
  const auto cancel_started = std::chrono::steady_clock::now();
  hmi_cancel_provider.Cancel();
  hmi_request.join();
  const auto cancel_elapsed = std::chrono::steady_clock::now() - cancel_started;
  hmi_cancel_service.call().Release();
  hmi_cancel_server->Shutdown();
  hmi_cancel_server->Wait();
  std::filesystem::remove_all(root);
  if (cancelled_succeeded || cancel_elapsed > std::chrono::milliseconds(500) ||
      cancelled_error != "HMI command cancelled") {
    std::cerr << "HMI Cancel did not TryCancel the active gRPC call: " << cancelled_error << '\n';
    return 1;
  }

  std::cout << "action provider deadline tests passed\n";
  return 0;
}
