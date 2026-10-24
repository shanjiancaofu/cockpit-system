#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cockpit {
namespace camera {

struct VideoDeviceInfo {
  std::string path;
  std::string driver;
  std::string card;
  std::string bus_info;
  std::string error;
  std::uint32_t capabilities = 0;
  std::uint32_t device_caps = 0;
  bool query_ok = false;
  bool supports_capture = false;
  bool supports_streaming = false;
};

struct FrameSizeInfo {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

struct PixelFormatInfo {
  std::uint32_t fourcc = 0;
  std::string fourcc_text;
  std::string description;
  std::vector<FrameSizeInfo> frame_sizes;
};

class V4l2Camera {
 public:
  static std::vector<VideoDeviceInfo> ListDevices(std::string* error);
  static std::vector<PixelFormatInfo> ListFormats(const std::string& device_path,
                                                  std::string* error);

 private:
  V4l2Camera() = default;
};

}  // namespace camera
}  // namespace cockpit
