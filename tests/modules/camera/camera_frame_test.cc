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

  if (!Check(frame.IsValid(), "valid camera frame was rejected") ||
      !Check(cockpit::camera::ToString(frame.format) == "bgrx",
             "unexpected camera format string") ||
      !Check(cockpit::camera::ToString(cockpit::camera::CameraPixelFormat::kNv12) == "nv12",
             "unexpected NV12 format string") ||
      !Check(cockpit::camera::ToString(cockpit::camera::CameraTimestampClock::kMonotonic) ==
                 "monotonic",
             "unexpected camera timestamp clock string")) {
    return 1;
  }

  frame.data.resize(15);
  if (!Check(!frame.IsValid(), "undersized BGRx frame was accepted")) {
    return 1;
  }
  frame.data.resize(16);
  frame.stride_bytes = 7;
  if (!Check(!frame.IsValid(), "BGRx frame with a short stride was accepted")) {
    return 1;
  }

  cockpit::camera::CameraFrame nv12;
  nv12.width = 4;
  nv12.height = 2;
  nv12.stride_bytes = 4;
  nv12.format = cockpit::camera::CameraPixelFormat::kNv12;
  nv12.data.resize(12);
  return Check(nv12.IsValid(), "valid NV12 frame was rejected") ? 0 : 1;
}
