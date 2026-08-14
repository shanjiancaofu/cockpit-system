#pragma once

#include <chrono>
#include <string>

#include "cockpit/modules/voice/actions/action_dispatcher.h"

namespace cockpit {
namespace voice {

enum class HmiCommand {
  kOpenCameraPreview,
  kPlayMusic,
};

class HmiCommandProvider {
 public:
  virtual ~HmiCommandProvider() = default;

  virtual bool SendCommand(HmiCommand command, const ActionExecutionContext& context,
                           std::string* response, std::string* error) = 0;
  virtual void Cancel() {
  }
};

const char* ToString(HmiCommand command);

}  // namespace voice
}  // namespace cockpit
