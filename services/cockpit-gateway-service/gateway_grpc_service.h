#pragma once

#include "gateway.grpc.pb.h"

#include <grpcpp/grpcpp.h>

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
  std::uint64_t version_ = 0;
  bool stopping_ = false;
  std::unique_ptr<grpc::Server> server_;
};

}  // namespace gateway
}  // namespace cockpit
