#include "cockpit/modules/camera/frames/camera_frame.h"

#include <limits>

namespace cockpit {
namespace camera {

namespace {

bool CheckedMultiply(std::size_t left, std::size_t right, std::size_t* result) {
  if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) {
    return false;
  }
  *result = left * right;
  return true;
}

std::size_t BytesPerPixel(CameraPixelFormat format) {
  switch (format) {
    case CameraPixelFormat::kRgb:
      return 3;
    case CameraPixelFormat::kBgrx:
      return 4;
    case CameraPixelFormat::kYuyv:
      return 2;
    case CameraPixelFormat::kSrgGb10:
      return 2;
    case CameraPixelFormat::kUnknown:
    case CameraPixelFormat::kMjpeg:
    case CameraPixelFormat::kNv12:
      return 0;
  }
  return 0;
}

}  // namespace

bool CameraFrame::HasValidLayout() const {
  if (width == 0 || height == 0 || stride_bytes == 0 || format == CameraPixelFormat::kUnknown) {
    return false;
  }

  const std::size_t bytes_per_pixel = BytesPerPixel(format);
  if (bytes_per_pixel != 0) {
    std::size_t minimum_stride = 0;
    return CheckedMultiply(width, bytes_per_pixel, &minimum_stride) &&
           static_cast<std::size_t>(stride_bytes) >= minimum_stride;
  }
  if (format == CameraPixelFormat::kNv12) {
    return height % 2U == 0U && stride_bytes >= width;
  }
  return format == CameraPixelFormat::kMjpeg;
}

bool CameraFrame::HasValidPayloadSize(std::size_t payload_size) const {
  if (!HasValidLayout() || payload_size == 0) {
    return false;
  }
  if (format == CameraPixelFormat::kMjpeg) {
    return true;
  }

  std::size_t required_size = 0;
  if (!CheckedMultiply(static_cast<std::size_t>(stride_bytes), height, &required_size)) {
    return false;
  }
  if (format == CameraPixelFormat::kNv12) {
    std::size_t chroma_size = 0;
    if (!CheckedMultiply(static_cast<std::size_t>(stride_bytes), height / 2U, &chroma_size) ||
        chroma_size > std::numeric_limits<std::size_t>::max() - required_size) {
      return false;
    }
    required_size += chroma_size;
  }
  return payload_size >= required_size;
}

bool CameraFrame::IsValid() const {
  return HasValidPayloadSize(data.size());
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
    case CameraPixelFormat::kSrgGb10:
      return "srg_gb10";
    case CameraPixelFormat::kUnknown:
      break;
  }
  return "unknown";
}

}  // namespace camera
}  // namespace cockpit
