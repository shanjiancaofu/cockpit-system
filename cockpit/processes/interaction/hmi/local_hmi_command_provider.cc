#include "local_hmi_command_provider.h"

#include "cockpit/core/logging/logger.h"

namespace cockpit {
namespace voice {

bool LocalHmiCommandProvider::SendCommand(HmiCommand command, std::string* response, std::string*) {
  const std::string command_name = ToString(command);
  LOG_INFO("local HMI bridge recorded command=" + command_name);
  if (response != nullptr) {
    *response = "HMI command recorded locally: " + command_name +
                ". Connect a Qt or Android bridge to execute it.";
  }
  return true;
}

}  // namespace voice
}  // namespace cockpit
