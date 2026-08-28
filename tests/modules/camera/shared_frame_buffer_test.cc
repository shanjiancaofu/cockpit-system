#include "cockpit/modules/camera/shared_memory/shared_frame_buffer.h"

#include <sys/wait.h>
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
  frame.source_timestamp_ns = static_cast<std::int64_t>(sequence * 1000000U);
  frame.received_at_ns = static_cast<std::int64_t>(sequence * 10000000U);
  frame.source_clock = cockpit::camera::CameraTimestampClock::kMonotonic;
  frame.source_timestamp_flags = 0x2000U;
  frame.source_timestamp_valid = true;
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

  if (!Check(reader->IsAvailable(), "shared frame reader did not detect its writer")) {
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
      !Check(output.source_timestamp_valid && output.source_timestamp_ns == 1000000 &&
                 output.received_at_ns == 10000000 &&
                 output.source_clock == cockpit::camera::CameraTimestampClock::kMonotonic &&
                 output.source_timestamp_flags == 0x2000U,
             "first shared frame timestamp metadata mismatch") ||
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
  if (!Check(!writer->Publish(std::move(oversized)), "oversized shared frame was accepted") ||
      !Check(writer->status().frames_rejected == 1, "shared frame rejection metric mismatch")) {
    return 1;
  }

  auto undersized = MakeFrame(4, 0x44);
  undersized.data.resize(15);
  if (!Check(!writer->Publish(std::move(undersized)), "undersized shared frame was accepted")) {
    return 1;
  }

  writer.reset();
  if (!Check(!reader->IsAvailable(), "shared frame reader did not detect writer shutdown")) {
    return 1;
  }

  cockpit::camera::SharedFrameBufferConfig stale_config;
  stale_config.name = "/cockpit_camera_stale_test_" + std::to_string(getpid());
  stale_config.max_frame_bytes = 64;
  const pid_t child = fork();
  if (child < 0) {
    std::cerr << "fork stale shared memory writer failed\n";
    return 1;
  }
  if (child == 0) {
    std::string child_error;
    auto stale_writer = cockpit::camera::SharedFrameWriter::Create(stale_config, &child_error);
    if (stale_writer == nullptr || !stale_writer->Publish(MakeFrame(1, 0x55))) {
      _exit(1);
    }
    _exit(0);
  }
  int child_status = 0;
  if (waitpid(child, &child_status, 0) != child || !WIFEXITED(child_status) ||
      WEXITSTATUS(child_status) != 0) {
    std::cerr << "stale shared memory writer child failed\n";
    return 1;
  }

  auto stale_reader = cockpit::camera::SharedFrameReader::Open(stale_config.name, &error);
  if (!Check(stale_reader != nullptr && stale_reader->IsAvailable(),
             "stale shared frame mapping was not available before recovery")) {
    return 1;
  }
  auto recovered_writer = cockpit::camera::SharedFrameWriter::Create(stale_config, &error);
  if (!Check(recovered_writer != nullptr, "stale shared frame mapping was not recovered")) {
    std::cerr << error << '\n';
    return 1;
  }
  return Check(!stale_reader->IsAvailable(),
               "stale shared frame reader was not invalidated during recovery")
             ? 0
             : 1;
}
