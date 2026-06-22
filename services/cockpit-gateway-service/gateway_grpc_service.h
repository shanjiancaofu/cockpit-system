#pragma once

#include "gateway.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace cockpit {
namespace gateway {

class GatewayGrpcService final : public proto::gateway::CockpitGateway::Service {
 public:
  GatewayGrpcService() = default;
  ~GatewayGrpcService() override;

  GatewayGrpcService(const GatewayGrpcService&) = delete;
  GatewayGrpcService& operator=(const GatewayGrpcService&) = delete;

  bool Start(const std::string& address);
  void PublishVehicleState(const proto::vehicle::VehicleState& state);
  void Shutdown();

 private:
  grpc::Status GetLatestVehicleState(
      grpc::ServerContext* context,
      const proto::common::Empty* request,
      proto::vehicle::VehicleState* response) override;
  grpc::Status ListTopics(
      grpc::ServerContext* context,
      const proto::gateway::ListTopicsRequest* request,
      proto::gateway::ListTopicsResponse* response) override;
  grpc::Status GetTopicInfo(
      grpc::ServerContext* context,
      const proto::gateway::GetTopicInfoRequest* request,
      proto::gateway::TopicMetadata* response) override;
  grpc::Status SubscribeCockpitEvents(
      grpc::ServerContext* context,
      const proto::gateway::SubscribeCockpitEventsRequest* request,
      grpc::ServerWriter<proto::gateway::CockpitEvent>* writer) override;

  std::mutex mutex_;
  std::condition_variable event_changed_;
  proto::gateway::CockpitEvent latest_event_;
  std::chrono::steady_clock::time_point latest_vehicle_update_;
  std::uint64_t version_ = 0;
  bool stopping_ = false;
  std::unique_ptr<grpc::Server> server_;
};

}  // namespace gateway
}  // namespace cockpit
