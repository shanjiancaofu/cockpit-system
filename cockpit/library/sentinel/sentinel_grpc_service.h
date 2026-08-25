#pragma once

#include <grpcpp/grpcpp.h>

#include <memory>
#include <string>

#include "cockpit/modules/sentinel/sentinel_service.h"
#include "sentinel.grpc.pb.h"

namespace cockpit {
namespace sentinel {

class SentinelGrpcService final : public proto::sentinel::SentinelControl::Service {
 public:
  explicit SentinelGrpcService(SentinelService& service);
  ~SentinelGrpcService() override;
  bool Start(const std::string& address);
  void Shutdown();

 private:
  grpc::Status Arm(grpc::ServerContext*, const proto::common::Empty*,
                   proto::sentinel::SentinelStatus*) override;
  grpc::Status Disarm(grpc::ServerContext*, const proto::common::Empty*,
                      proto::sentinel::SentinelStatus*) override;
  grpc::Status GetStatus(grpc::ServerContext*, const proto::common::Empty*,
                         proto::sentinel::SentinelStatus*) override;
  static void FillStatus(const SentinelStatus& status, proto::sentinel::SentinelStatus* response);

  SentinelService& service_;
  std::unique_ptr<grpc::Server> server_;
};

}  // namespace sentinel
}  // namespace cockpit
