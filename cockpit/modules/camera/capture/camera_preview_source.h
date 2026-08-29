#pragma once

#include <functional>
#include <string>

#include "cockpit/modules/camera/frames/camera_frame.h"

namespace cockpit {
namespace camera {

enum class CameraCapturePipeline {
  kArgusIsp,
  kUvc,
  kSoftwareIsp,
  kSynthetic,
};

enum class CameraUvcInputFormat {
  kMjpeg,
  kYuyv,
};

struct CameraPreviewConfig {
  std::string device = "/dev/video0";
  std::uint32_t width = 1920;
  std::uint32_t height = 1080;
  std::uint32_t fps = 30;
  CameraPixelFormat output_format = CameraPixelFormat::kBgrx;
};

class CameraPreviewSource {
 public:
  using FrameCallback = std::function<void(CameraFrame)>;

  virtual ~CameraPreviewSource() = default;

  virtual bool Start(const CameraPreviewConfig& config, FrameCallback callback,
                     std::string* error) = 0;
  virtual void Stop() = 0;
  virtual bool IsRunning() const = 0;
};

}  // namespace camera
}  // namespace cockpit
