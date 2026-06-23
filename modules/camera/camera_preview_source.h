#pragma once

#include <functional>
#include <string>

#include "modules/camera/camera_frame.h"

namespace cockpit {
namespace camera {

struct CameraPreviewConfig {
  std::string device = "/dev/video0";
  std::uint32_t width = 640;
  std::uint32_t height = 480;
  std::uint32_t fps = 30;
  CameraPixelFormat output_format = CameraPixelFormat::kBgrx;
};

class CameraPreviewSource {
 public:
  using FrameCallback = std::function<void(const CameraFrame&)>;

  virtual ~CameraPreviewSource() = default;

  virtual bool Start(const CameraPreviewConfig& config, FrameCallback callback,
                     std::string* error) = 0;
  virtual void Stop() = 0;
  virtual bool IsRunning() const = 0;
};

}  // namespace camera
}  // namespace cockpit
