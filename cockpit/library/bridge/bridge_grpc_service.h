#pragma once

#include <grpcpp/grpcpp.h>

#include <memory>
#include <string>

#include "bridge.grpc.pb.h"
#include "cockpit/modules/bridge/bridge_service.h"

namespace cockpit::bridge {

class BridgeGrpcService final : public proto::bridge::BridgeControl::Service {
 public:
  explicit BridgeGrpcService(BridgeService& service);
  ~BridgeGrpcService() override;
  bool Start(const std::string& address);
  void Shutdown();

 private:
  grpc::Status SubmitGoal(grpc::ServerContext*, const proto::bridge::SubmitBridgeGoalRequest*,
                          proto::bridge::BridgeStatus*) override;
  grpc::Status CancelGoal(grpc::ServerContext*, const proto::bridge::CancelBridgeGoalRequest*,
                          proto::bridge::BridgeStatus*) override;
  grpc::Status GetStatus(grpc::ServerContext*, const proto::common::Empty*,
                         proto::bridge::BridgeStatus*) override;
  static void FillStatus(const BridgeStatus&, proto::bridge::BridgeStatus*);

  BridgeService& service_;
  std::unique_ptr<grpc::Server> server_;
};

}  // namespace cockpit::bridge
