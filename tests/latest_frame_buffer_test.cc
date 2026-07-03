#include "modules/camera/frames/latest_frame_buffer.h"

#include <iostream>
#include <utility>

namespace {

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

cockpit::camera::CameraFrame MakeFrame(std::uint64_t sequence, std::uint8_t value) {
  cockpit::camera::CameraFrame frame;
  frame.sequence = sequence;
  frame.timestamp_ms = sequence * 10;
  frame.width = 2;
  frame.height = 2;
  frame.stride_bytes = 8;
  frame.format = cockpit::camera::CameraPixelFormat::kBgrx;
  frame.data.resize(16, value);
  return frame;
}

}  // namespace

int main() {
  cockpit::camera::LatestFrameBuffer buffer;
  cockpit::camera::CameraFrame frame;
  if (!Check(!buffer.ReadLatest(&frame), "empty latest frame buffer returned a frame") ||
      !Check(!buffer.Publish({}), "latest frame buffer accepted an invalid frame")) {
    return 1;
  }

  auto first = MakeFrame(1, 0x11);
  if (!Check(buffer.Publish(std::move(first)), "first camera frame publish failed")) {
    return 1;
  }

  auto second = MakeFrame(2, 0x22);
  if (!Check(buffer.Publish(std::move(second)), "second camera frame publish failed")) {
    return 1;
  }

  std::uint64_t generation = 0;
  if (!Check(buffer.ReadLatest(&frame, &generation), "latest camera frame read failed") ||
      !Check(frame.sequence == 2, "latest camera frame was not retained") ||
      !Check(frame.data.front() == 0x22, "latest camera frame payload mismatch") ||
      !Check(generation == 2, "latest camera frame generation mismatch")) {
    return 1;
  }

  const auto status = buffer.status();
  if (!Check(status.frames_published == 2, "latest frame publish count mismatch") ||
      !Check(status.frames_replaced == 1, "latest frame replace count mismatch") ||
      !Check(status.has_frame, "latest frame status lost stored frame")) {
    return 1;
  }

  buffer.Clear();
  return Check(!buffer.status().has_frame, "latest frame buffer clear failed") ? 0 : 1;
}
