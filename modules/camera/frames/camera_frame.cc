#include "modules/camera/frames/camera_frame.h"

namespace cockpit {
namespace camera {

bool CameraFrame::IsValid() const {
  return width > 0 && height > 0 && stride_bytes > 0 && format != CameraPixelFormat::kUnknown &&
         !data.empty();
}

std::string ToString(CameraPixelFormat format) {
  switch (format) {
    case CameraPixelFormat::kRgb:
      return "rgb";
    case CameraPixelFormat::kBgrx:
      return "bgrx";
    case CameraPixelFormat::kYuyv:
      return "yuyv";
    case CameraPixelFormat::kMjpeg:
      return "mjpeg";
    case CameraPixelFormat::kNv12:
      return "nv12";
    case CameraPixelFormat::kUnknown:
      break;
  }
  return "unknown";
}

}  // namespace camera
}  // namespace cockpit
