#pragma once

#include <string>

namespace cockpit {
namespace voice {

enum class HmiCommand {
  kOpenCameraPreview,
  kPlayMusic,
};

class HmiCommandProvider {
 public:
  virtual ~HmiCommandProvider() = default;

  virtual bool SendCommand(HmiCommand command, std::string* response, std::string* error) = 0;
};

const char* ToString(HmiCommand command);

}  // namespace voice
}  // namespace cockpit
