#pragma once

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

#include "cockpit/core/base/macros.h"
#include "cockpit/modules/can/can_link_status.h"
#include "cockpit/modules/vehicle/chassis_event.h"
#include "cockpit/modules/vehicle/chassis_state.h"
#include "cockpit/modules/vehicle/vehicle_state.h"
#include "vehicle_state.grpc.pb.h"

namespace cockpit {
namespace vehicle {

class VehicleGrpcService final : public proto::vehicle::VehicleDataService::Service {
 public:
  VehicleGrpcService() = default;
  ~VehicleGrpcService() override;

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(VehicleGrpcService);

  bool Start(const std::string& address);
  void Publish(const VehicleState& state);
  void PublishChassisState(const ChassisState& state);
  void PublishEvent(const ChassisEvent& event);
  bool WaitForChassisStateSubscriber(std::chrono::milliseconds timeout);
  bool WaitForEventSubscriber(std::chrono::milliseconds timeout);
  void PublishLinkStatus(const can::CanLinkStatus& status);
  void Shutdown();

 private:
  grpc::Status SubscribeVehicleState(
      grpc::ServerContext* context, const proto::vehicle::SubscribeVehicleStateRequest* request,
      grpc::ServerWriter<proto::vehicle::VehicleState>* writer) override;
  grpc::Status GetStatus(grpc::ServerContext* context, const proto::common::Empty* request,
                         proto::vehicle::CanLinkStatus* response) override;
  grpc::Status SubscribeChassisState(
      grpc::ServerContext* context, const proto::vehicle::SubscribeChassisStateRequest* request,
      grpc::ServerWriter<proto::vehicle::ChassisState>* writer) override;
  grpc::Status SubscribeChassisEvents(
      grpc::ServerContext* context, const proto::vehicle::SubscribeChassisEventsRequest* request,
      grpc::ServerWriter<proto::vehicle::ChassisEvent>* writer) override;

  struct VersionedEvent {
    std::uint64_t version = 0;
    proto::vehicle::ChassisEvent event;
  };

  std::mutex mutex_;
  std::condition_variable state_changed_;
  VehicleState latest_state_;
  ChassisState latest_chassis_state_;
  can::CanLinkStatus link_status_;
  std::uint64_t version_ = 0;
  std::uint64_t chassis_state_version_ = 0;
  std::uint64_t event_version_ = 0;
  std::uint64_t dropped_events_ = 0;
  std::size_t event_subscribers_ = 0;
  std::size_t chassis_state_subscribers_ = 0;
  std::deque<VersionedEvent> events_;
  bool stopping_ = false;
  std::unique_ptr<grpc::Server> server_;

  static constexpr std::size_t kEventCapacity = 64;
};

}  // namespace vehicle
}  // namespace cockpit
