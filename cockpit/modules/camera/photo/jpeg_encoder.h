#pragma once

#include <filesystem>
#include <string>

#include "cockpit/modules/camera/frames/camera_frame.h"

namespace cockpit {
namespace camera {

class JpegEncoder {
 public:
  static bool IsAvailable();
  static bool Encode(const CameraFrame& frame, const std::filesystem::path& output_path,
                     int quality, std::string* error);
};

}  // namespace camera
}  // namespace cockpit
