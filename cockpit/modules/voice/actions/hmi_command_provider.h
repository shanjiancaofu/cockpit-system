#pragma once

#include <chrono>
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

  virtual bool SendCommand(HmiCommand command, std::chrono::steady_clock::time_point deadline,
                           std::string* response, std::string* error) = 0;
  virtual void Cancel() {
  }
};

const char* ToString(HmiCommand command);

}  // namespace voice
}  // namespace cockpit
