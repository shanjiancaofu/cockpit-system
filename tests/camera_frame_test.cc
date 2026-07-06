#include "cockpit/modules/camera/frames/camera_frame.h"

#include <iostream>
#include <string>

namespace {

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  cockpit::camera::CameraFrame frame;
  if (!Check(!frame.IsValid(), "empty camera frame was accepted")) {
    return 1;
  }

  frame.sequence = 7;
  frame.timestamp_ms = 1234;
  frame.width = 2;
  frame.height = 2;
  frame.stride_bytes = 8;
  frame.format = cockpit::camera::CameraPixelFormat::kBgrx;
  frame.data.resize(16, 0xff);

  return Check(frame.IsValid(), "valid camera frame was rejected") &&
                 Check(cockpit::camera::ToString(frame.format) == "bgrx",
                       "unexpected camera format string") &&
                 Check(
                     cockpit::camera::ToString(cockpit::camera::CameraPixelFormat::kNv12) == "nv12",
                     "unexpected NV12 format string")
             ? 0
             : 1;
}
