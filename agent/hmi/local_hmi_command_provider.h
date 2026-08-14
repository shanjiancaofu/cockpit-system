#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "cockpit/modules/voice/actions/hmi_command_provider.h"
#include "hmi.grpc.pb.h"

namespace cockpit {
namespace voice {

class LocalHmiCommandProvider final : public HmiCommandProvider {
 public:
  explicit LocalHmiCommandProvider(const std::string& address);

  bool SendCommand(HmiCommand command, const ActionExecutionContext& context, std::string* response,
                   std::string* error) override;
  void Cancel() override;

 private:
  std::unique_ptr<proto::hmi::HmiControl::Stub> stub_;
  std::atomic<std::uint64_t> cancellation_generation_{0};
  std::mutex cancellation_mutex_;
  grpc::ClientContext* active_context_ = nullptr;
};

}  // namespace voice
}  // namespace cockpit
