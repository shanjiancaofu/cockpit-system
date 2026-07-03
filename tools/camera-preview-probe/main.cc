#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>

#include "core/runtime/ServiceRuntime.h"
#include "modules/camera/capture/gstreamer_preview_pipeline.h"

namespace {

int Finish(const cockpit::runtime::ServiceRuntime& runtime, int result) {
  runtime.MarkStopped();
  return result;
}

void PrintUsage() {
  std::cout << "camera-preview-probe --device /dev/video0 [--frames 30] "
               "[--width 640] [--height 480] [--fps 30] "
               "[--timeout-ms 5000] [--config configs/config.yaml]\n";
}

struct ProbeState {
  std::mutex mutex;
  std::condition_variable condition;
  std::uint64_t first_timestamp_ms = 0;
  std::uint64_t last_timestamp_ms = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t stride_bytes = 0;
  std::size_t bytes = 0;
  int frames = 0;
};

}  // namespace

int main(int argc, char** argv) {
  const auto runtime = cockpit::runtime::ServiceRuntime::Create(argc, argv, "camera-preview-probe");
  if (runtime.args().HasFlag("help")) {
    PrintUsage();
    return Finish(runtime, 0);
  }

  cockpit::camera::CameraPreviewConfig config;
  config.device = runtime.args().GetString("device", "/dev/video0");
  config.width = static_cast<std::uint32_t>(runtime.args().GetInt("width", 640));
  config.height = static_cast<std::uint32_t>(runtime.args().GetInt("height", 480));
  config.fps = static_cast<std::uint32_t>(runtime.args().GetInt("fps", 30));

  const int target_frames = runtime.args().GetInt("frames", 30);
  const int timeout_ms = runtime.args().GetInt("timeout-ms", 5000);
  if (target_frames <= 0 || timeout_ms <= 0) {
    std::cerr << "frames and timeout-ms must be positive\n";
    return Finish(runtime, 2);
  }

  ProbeState state;
  cockpit::camera::GstreamerPreviewPipeline preview;
  std::string error;
  const bool started = preview.Start(
      config,
      [&state, target_frames](const cockpit::camera::CameraFrame& frame) {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.frames == 0) {
          state.first_timestamp_ms = frame.timestamp_ms;
        }
        state.last_timestamp_ms = frame.timestamp_ms;
        state.width = frame.width;
        state.height = frame.height;
        state.stride_bytes = frame.stride_bytes;
        state.bytes = frame.data.size();
        ++state.frames;
        if (state.frames >= target_frames) {
          state.condition.notify_one();
        }
      },
      &error);
  if (!started) {
    std::cerr << error << '\n';
    return Finish(runtime, 1);
  }

  bool complete = false;
  {
    std::unique_lock<std::mutex> lock(state.mutex);
    complete = state.condition.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                        [&state, target_frames] {
                                          return state.frames >= target_frames;
                                        });
  }
  preview.Stop();

  std::lock_guard<std::mutex> lock(state.mutex);
  if (state.frames == 0) {
    std::cerr << "no preview frames received from " << config.device << '\n';
    return Finish(runtime, 1);
  }

  const double span_s =
      state.last_timestamp_ms > state.first_timestamp_ms
          ? static_cast<double>(state.last_timestamp_ms - state.first_timestamp_ms) / 1000.0
          : 0.0;
  const double fps =
      span_s > 0.0 && state.frames > 1 ? static_cast<double>(state.frames - 1) / span_s : 0.0;

  std::cout << "captured " << state.frames << " frame(s) from " << config.device
            << " size=" << state.width << 'x' << state.height << " stride=" << state.stride_bytes
            << " bytes=" << state.bytes << " fps=" << fps << '\n';
  if (!complete) {
    std::cerr << "timed out before requested frame count\n";
    return Finish(runtime, 1);
  }
  return Finish(runtime, 0);
}
