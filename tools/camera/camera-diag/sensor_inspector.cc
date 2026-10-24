#include <fcntl.h>
#include <linux/videodev2.h>

#include <cerrno>
#include <cstring>
#include <filesystem>

#include "diagnostics.h"

#include "cockpit/drivers/v4l2/v4l2_camera.h"

namespace cockpit::camera_diag {
Json InspectSensor(const std::string& device, std::vector<std::string>* errors) {
  std::string error;
  const auto formats = camera::V4l2Camera::ListFormats(device, &error);
  if (!error.empty()) errors->push_back(error);
  Fd fd(open(device.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC));
  std::vector<Json> modes;
  for (const auto& format : formats) {
    for (const auto& size : format.frame_sizes) {
      v4l2_frmivalenum interval{};
      interval.pixel_format = format.fourcc;
      interval.width = size.width;
      interval.height = size.height;
      std::vector<Json> intervals;
      while (Ioctl(fd.value, VIDIOC_ENUM_FRAMEINTERVALS, &interval) == 0) {
        if (interval.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
          intervals.push_back(
              Object({{"numerator", Number(interval.discrete.numerator)},
                      {"denominator", Number(interval.discrete.denominator)},
                      {"fps", interval.discrete.numerator
                                  ? Number(static_cast<double>(interval.discrete.denominator) /
                                           interval.discrete.numerator)
                                  : "null"}}));
        } else {
          intervals.push_back(
              Object({{"type", Quote("stepwise_or_continuous")},
                      {"min_numerator", Number(interval.stepwise.min.numerator)},
                      {"min_denominator", Number(interval.stepwise.min.denominator)},
                      {"max_numerator", Number(interval.stepwise.max.numerator)},
                      {"max_denominator", Number(interval.stepwise.max.denominator)},
                      {"step_numerator", Number(interval.stepwise.step.numerator)},
                      {"step_denominator", Number(interval.stepwise.step.denominator)}}));
          break;
        }
        ++interval.index;
      }
      if (errno != EINVAL && intervals.empty())
        errors->push_back(device + " frame intervals: " + std::strerror(errno));
      modes.push_back(Object({{"fourcc", Quote(format.fourcc_text)},
                              {"width", Number(size.width)},
                              {"height", Number(size.height)},
                              {"intervals", Array(intervals)}}));
    }
  }
  if (modes.empty()) errors->push_back(device + ": no discrete sensor modes enumerated");
  std::vector<Json> sensors;
  // Read the bound sensor's device-tree modes, without changing pad format or sensor_mode.
  for (const auto& path : DevicePaths("v4l-subdev")) {
    const auto name = std::filesystem::path(path).filename().string();
    const auto base = std::filesystem::path("/sys/class/video4linux") / name;
    std::vector<Json> dt_modes;
    const auto node = base / "device/of_node";
    std::error_code ec;
    if (std::filesystem::exists(node, ec)) {
      for (const auto& entry : std::filesystem::directory_iterator(node)) {
        const auto mode_name = entry.path().filename().string();
        if (mode_name.compare(0, 4, "mode") != 0 || !entry.is_directory()) continue;
        dt_modes.push_back(Object(
            {{"name", Quote(mode_name)},
             {"width", Quote(ReadText((entry.path() / "active_w").string()))},
             {"height", Quote(ReadText((entry.path() / "active_h").string()))},
             {"max_framerate", Quote(ReadText((entry.path() / "max_framerate").string()))},
             {"framerate_factor", Quote(ReadText((entry.path() / "framerate_factor").string()))}}));
      }
    }
    sensors.push_back(Object({{"path", Quote(path)},
                              {"name", Quote(ReadText((base / "name").string()))},
                              {"compatible", Quote(ReadText((node / "compatible").string()))},
                              {"device_tree_modes", Array(dt_modes)}}));
  }
  return Object(
      {{"device", Quote(device)}, {"modes", Array(modes)}, {"subdevices", Array(sensors)}});
}
}  // namespace cockpit::camera_diag
