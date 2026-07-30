#pragma once

#include <memory>
#include <string>

#include "cockpit/modules/voice/actions/hmi_command_provider.h"
#include "hmi.grpc.pb.h"

namespace cockpit {
namespace voice {

class LocalHmiCommandProvider final : public HmiCommandProvider {
 public:
  explicit LocalHmiCommandProvider(const std::string& address);

  bool SendCommand(HmiCommand command, std::string* response, std::string* error) override;

 private:
  std::unique_ptr<proto::hmi::HmiControl::Stub> stub_;
};

}  // namespace voice
}  // namespace cockpit
