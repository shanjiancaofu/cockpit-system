#include "modules/camera/shared_memory/shared_frame_buffer.h"

#include <unistd.h>

#include <iostream>
#include <string>
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
  cockpit::camera::SharedFrameBufferConfig config;
  config.name = "/cockpit_camera_test_" + std::to_string(getpid());
  config.max_frame_bytes = 64;

  std::string error;
  auto writer = cockpit::camera::SharedFrameWriter::Create(config, &error);
  if (!Check(writer != nullptr, "create shared frame writer failed")) {
    std::cerr << error << '\n';
    return 1;
  }
  auto reader = cockpit::camera::SharedFrameReader::Open(config.name, &error);
  if (!Check(reader != nullptr, "open shared frame reader failed")) {
    std::cerr << error << '\n';
    return 1;
  }

  cockpit::camera::CameraFrame output;
  std::uint64_t generation = 0;
  if (!Check(!reader->ReadLatest(&output, &generation, &error),
             "empty shared frame buffer returned a frame") ||
      !Check(writer->Publish(MakeFrame(1, 0x11)), "publish first shared frame failed") ||
      !Check(reader->ReadLatest(&output, &generation, &error), "read first shared frame failed") ||
      !Check(output.sequence == 1 && output.data.front() == 0x11,
             "first shared frame payload mismatch") ||
      !Check(generation == 1, "first shared frame generation mismatch") ||
      !Check(writer->Publish(MakeFrame(2, 0x22)), "publish second shared frame failed") ||
      !Check(reader->ReadLatest(&output, &generation, &error), "read second shared frame failed") ||
      !Check(output.sequence == 2 && output.data.front() == 0x22,
             "second shared frame payload mismatch") ||
      !Check(generation == 2, "second shared frame generation mismatch")) {
    return 1;
  }

  auto oversized = MakeFrame(3, 0x33);
  oversized.data.resize(65);
  return Check(!writer->Publish(std::move(oversized)), "oversized shared frame was accepted") &&
                 Check(writer->status().frames_rejected == 1,
                       "shared frame rejection metric mismatch")
             ? 0
             : 1;
}
