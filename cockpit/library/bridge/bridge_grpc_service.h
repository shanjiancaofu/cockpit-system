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
  grpc::Status SubmitNavigationGoal(grpc::ServerContext*,
                                    const proto::bridge::SubmitNavigationGoalRequest*,
                                    proto::bridge::NavigationStatus*) override;
  grpc::Status CancelNavigationGoal(grpc::ServerContext*,
                                    const proto::bridge::CancelNavigationGoalRequest*,
                                    proto::bridge::NavigationStatus*) override;
  grpc::Status GetNavigationStatus(grpc::ServerContext*, const proto::common::Empty*,
                                   proto::bridge::NavigationStatus*) override;
  static void FillStatus(const NavigationStatus&, proto::bridge::NavigationStatus*);

  BridgeService& service_;
  std::unique_ptr<grpc::Server> server_;
};

}  // namespace cockpit::bridge
