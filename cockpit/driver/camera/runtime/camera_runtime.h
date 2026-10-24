#pragma once

#include <memory>
#include <string>

namespace cockpit {
namespace camera {

class CameraRuntime {
 public:
  CameraRuntime();
  ~CameraRuntime();

  CameraRuntime(const CameraRuntime&) = delete;
  CameraRuntime& operator=(const CameraRuntime&) = delete;

  bool Start(const std::string& config_path);
  void Stop();
  int Poll();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace camera
}  // namespace cockpit
