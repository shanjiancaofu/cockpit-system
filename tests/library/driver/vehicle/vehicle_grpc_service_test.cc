#include "cockpit/library/driver/vehicle/vehicle_grpc_service.h"

#include <grpcpp/grpcpp.h>
#include <unistd.h>

#include <chrono>
#include <iostream>
#include <string>

namespace {

bool ReadStatus(const std::string& address, cockpit::proto::vehicle::CanLinkStatus* response) {
  auto stub = cockpit::proto::vehicle::VehicleDataService::NewStub(
      grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));
  cockpit::proto::common::Empty request;
  grpc::ClientContext context;
  context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(1));
  return stub->GetStatus(&context, request, response).ok();
}

}  // namespace

int main() {
  const std::string address = "unix:/tmp/cockpit-vehicle-health-" +
                              std::to_string(static_cast<long long>(getpid())) + ".sock";
  cockpit::vehicle::VehicleGrpcService service;
  if (!service.Start(address)) {
    std::cerr << "vehicle gRPC service did not start\n";
    return 1;
  }

  cockpit::can::CanLinkStatus link;
  link.interface_name = "vcan0";
  link.fd_enabled = true;
  link.state = cockpit::can::CanCommunicationState::kOnline;
  link.last_rx_timestamp_ms =
      static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count());
  link.rx_frames = 10;
  link.decoded_frames = 8;
  link.invalid_frames = 2;
  service.PublishLinkStatus(link);

  cockpit::proto::vehicle::CanLinkStatus status;
  if (!ReadStatus(address, &status) || status.interface_name() != "vcan0" || !status.fd_enabled() ||
      status.rx_frames() != 10 || status.decoded_frames() != 8 || status.invalid_frames() != 2 ||
      status.health().state() != cockpit::proto::common::SERVICE_HEALTH_STATE_OK) {
    std::cerr << "online CAN health is invalid\n";
    return 1;
  }

  link.state = cockpit::can::CanCommunicationState::kFaulted;
  link.bus_off_count = 1;
  link.error_frames = 1;
  link.last_error = "can0 bus-off";
  service.PublishLinkStatus(link);
  if (!ReadStatus(address, &status) || status.bus_off_count() != 1 ||
      status.health().state() != cockpit::proto::common::SERVICE_HEALTH_STATE_FAULTED ||
      status.health().message() != "can0 bus-off") {
    std::cerr << "faulted CAN health is invalid\n";
    return 1;
  }
  service.Shutdown();
  std::cout << "vehicle gRPC health tests passed\n";
  return 0;
}
