#pragma once

#include "modules/voice/hmi_command_provider.h"

namespace cockpit {
namespace voice {

class LocalHmiCommandProvider final : public HmiCommandProvider {
 public:
  bool SendCommand(HmiCommand command, std::string* response,
                   std::string* error) override;
};

}  // namespace voice
}  // namespace cockpit
