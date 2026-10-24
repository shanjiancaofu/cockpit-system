#include <fcntl.h>
#include <linux/videodev2.h>

#include <cerrno>
#include <cstring>

#include "diagnostics.h"

namespace cockpit::camera_diag {
bool SetSensorTestPattern(const std::string& device, int value, std::string* error) {
  Fd fd(open(device.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC));
  v4l2_queryctrl query{};
  query.id = V4L2_CID_TEST_PATTERN;
  if (fd.value < 0 || Ioctl(fd.value, VIDIOC_QUERYCTRL, &query) < 0 ||
      (query.flags & (V4L2_CTRL_FLAG_DISABLED | V4L2_CTRL_FLAG_READ_ONLY))) {
    if (error) *error = "test_pattern unavailable or not writable on " + device;
    return false;
  }
  if (value < query.minimum || value > query.maximum) {
    if (error) *error = "test_pattern value outside driver range";
    return false;
  }
  v4l2_control control{};
  control.id = query.id;
  control.value = value;
  if (Ioctl(fd.value, VIDIOC_S_CTRL, &control) < 0) {
    if (error) *error = std::strerror(errno);
    return false;
  }
  return true;
}
Json InspectControls(const std::string& device, std::vector<std::string>* errors) {
  Fd fd(open(device.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC));
  if (fd.value < 0) {
    errors->push_back(device + " controls: " + std::strerror(errno));
    return "[]";
  }
  std::vector<Json> controls;
  v4l2_query_ext_ctrl query{};
  query.id = V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
  while (Ioctl(fd.value, VIDIOC_QUERY_EXT_CTRL, &query) == 0) {
    Json current = "null";
    std::string read_status = "not_scalar";
    if (!(query.flags &
          (V4L2_CTRL_FLAG_HAS_PAYLOAD | V4L2_CTRL_FLAG_DISABLED | V4L2_CTRL_FLAG_WRITE_ONLY)) &&
        query.type != V4L2_CTRL_TYPE_BUTTON && query.type != V4L2_CTRL_TYPE_CTRL_CLASS) {
      v4l2_ext_control control{};
      control.id = query.id;
      v4l2_ext_controls values{};
      values.which = V4L2_CTRL_WHICH_CUR_VAL;
      values.count = 1;
      values.controls = &control;
      if (Ioctl(fd.value, VIDIOC_G_EXT_CTRLS, &values) == 0) {
        current = query.type == V4L2_CTRL_TYPE_INTEGER64 ? Number(control.value64)
                                                         : Number(control.value);
        read_status = "ok";
      } else {
        read_status = std::strerror(errno);
        errors->push_back(device + " control " + query.name + ": " + read_status);
      }
    }
    std::vector<Json> menu;
    if (query.type == V4L2_CTRL_TYPE_MENU || query.type == V4L2_CTRL_TYPE_INTEGER_MENU) {
      for (auto index = query.minimum; index <= query.maximum && index - query.minimum < 4096;
           ++index) {
        v4l2_querymenu item{};
        item.id = query.id;
        item.index = index;
        if (Ioctl(fd.value, VIDIOC_QUERYMENU, &item) == 0)
          menu.push_back(Object({{"index", Number(index)},
                                 {"value", query.type == V4L2_CTRL_TYPE_INTEGER_MENU
                                               ? Number(item.value)
                                               : Quote(reinterpret_cast<char*>(item.name))}}));
      }
    }
    controls.push_back(
        Object({{"id", Number(query.id)},
                {"name", Quote(query.name)},
                {"type", Number(query.type)},
                {"minimum", Number(query.minimum)},
                {"maximum", Number(query.maximum)},
                {"step", Number(query.step)},
                {"default", Number(query.default_value)},
                {"flags", Number(query.flags)},
                {"current", current},
                {"read_status", Quote(read_status)},
                {"menu", Array(menu)},
                {"test_pattern_interface", Boolean(query.id == V4L2_CID_TEST_PATTERN)}}));
    query.id |= V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
  }
  // CSI bridge subdevices may legitimately implement no control ioctl.
  if (errno != EINVAL && !(errno == ENOTTY && device.find("/dev/v4l-subdev") == 0))
    errors->push_back(device + " enumerate controls: " + std::strerror(errno));
  return Array(controls);
}
}  // namespace cockpit::camera_diag
