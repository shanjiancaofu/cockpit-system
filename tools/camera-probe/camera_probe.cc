#include "camera_probe.h"

#include <iostream>
#include <string>
#include <vector>

#include "cockpit/core/runtime/process_runtime.h"
#include "cockpit/drivers/v4l2/v4l2_camera.h"

namespace {

void PrintUsage() {
  std::cout << "camera-probe [--list] [--device /dev/video0 --formats] "
               "[--config configs/development.yaml]\n";
}

void PrintDevice(const cockpit::camera::VideoDeviceInfo& device) {
  if (!device.query_ok) {
    std::cout << device.path << " query=failed error=\"" << device.error << "\"\n";
    return;
  }
  std::cout << device.path << " card=\"" << device.card << "\""
            << " driver=\"" << device.driver << "\""
            << " bus=\"" << device.bus_info << "\""
            << " capture=" << (device.supports_capture ? "yes" : "no")
            << " streaming=" << (device.supports_streaming ? "yes" : "no") << " caps=0x" << std::hex
            << device.device_caps << std::dec << '\n';
}

void PrintFormats(const std::vector<cockpit::camera::PixelFormatInfo>& formats) {
  for (const auto& format : formats) {
    std::cout << format.fourcc_text << " - " << format.description;
    if (format.frame_sizes.empty()) {
      std::cout << '\n';
      continue;
    }
    std::cout << " sizes=";
    for (std::size_t i = 0; i < format.frame_sizes.size(); ++i) {
      if (i > 0) {
        std::cout << ',';
      }
      std::cout << format.frame_sizes[i].width << 'x' << format.frame_sizes[i].height;
    }
    std::cout << '\n';
  }
}

int ListDevices(const cockpit::runtime::ProcessRuntime& runtime) {
  static_cast<void>(runtime);
  std::string error;
  const auto devices = cockpit::camera::V4l2Camera::ListDevices(&error);
  if (!error.empty() && devices.empty()) {
    std::cerr << error << '\n';
    return 1;
  }
  if (devices.empty()) {
    std::cout << "no V4L2 video devices found\n";
    return 0;
  }
  for (const auto& device : devices) {
    PrintDevice(device);
  }
  return 0;
}

int ListFormats(const cockpit::runtime::ProcessRuntime& runtime) {
  const std::string device = runtime.args().GetString("device", "/dev/video0");
  std::string error;
  const auto formats = cockpit::camera::V4l2Camera::ListFormats(device, &error);
  if (!error.empty()) {
    std::cerr << error << '\n';
    return 1;
  }
  if (formats.empty()) {
    std::cout << "no capture formats found for " << device << '\n';
    return 0;
  }
  PrintFormats(formats);
  return 0;
}

}  // namespace

int cockpit::camera_probe::ProbeCamera(const cockpit::runtime::ProcessRuntime& runtime) {
  if (runtime.args().HasFlag("help")) {
    PrintUsage();
    return 0;
  }
  if (runtime.args().HasFlag("formats")) {
    return ListFormats(runtime);
  }
  return ListDevices(runtime);
}
