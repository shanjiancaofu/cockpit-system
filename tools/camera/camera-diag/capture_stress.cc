#include <linux/videodev2.h>

#include <chrono>

#include "diagnostics.h"

#include "cockpit/drivers/v4l2/v4l2_mmap_capture.h"

namespace cockpit::camera_diag {
Json CaptureStress(const Options& options, bool* passed, std::vector<std::string>* errors) {
  std::vector<Json> runs;
  *passed = true;
  for (int cycle = 0; cycle < options.cycles; ++cycle) {
    camera::V4l2MmapCapture capture;
    camera::V4l2MmapConfig config;
    config.device = options.device;
    config.width = options.width;
    config.height = options.height;
    config.fps = options.fps;
    std::string error;
    const auto start = std::chrono::steady_clock::now();
    const bool started = capture.Start(config, &error);
    FrameStats stats;
    int timeouts = 0, not_ready = 0, io_errors = 0;
    std::uint32_t timestamp_flags = 0;
    const auto width = capture.width(), height = capture.height();
    const auto deadline =
        start + std::chrono::milliseconds(options.timeout_ms) +
        std::chrono::duration<double>(static_cast<double>(options.frames) / options.fps * 2.0);
    if (started) {
      while (stats.frames < static_cast<std::uint64_t>(options.frames)) {
        if (std::chrono::steady_clock::now() > deadline) {
          ++timeouts;
          error = "capture cycle deadline exceeded";
          break;
        }
        camera::V4l2RawFrame frame;
        if (!capture.WaitFrame(&frame, options.timeout_ms, &error)) {
          if (capture.last_wait_status() == camera::V4l2WaitStatus::kNotReady) {
            ++not_ready;
            continue;
          }
          if (capture.last_wait_status() == camera::V4l2WaitStatus::kTimeout)
            ++timeouts;
          else
            ++io_errors;
          break;
        }
        stats.Observe(frame.sequence, frame.timestamp_ns,
                      (frame.buffer_flags & V4L2_BUF_FLAG_ERROR) != 0);
        timestamp_flags = frame.timestamp_flags;
      }
    }
    std::string stop_error;
    const bool stopped = capture.Stop(&stop_error);
    if (!stopped) {
      ++io_errors;
      error += " stop: " + stop_error;
    }
    const bool dimensions_match = width == config.width && height == config.height;
    const bool ok = started && stats.frames == static_cast<std::uint64_t>(options.frames) &&
                    stats.healthy() && timeouts == 0 && io_errors == 0 && dimensions_match &&
                    (options.frames < 2 ||
                     (stats.fps() >= options.fps * 0.95 && stats.fps() <= options.fps * 1.05));
    if (!ok) {
      *passed = false;
      errors->push_back(
          "cycle " + std::to_string(cycle) + ": " +
          (error.empty() ? "frame count, dimensions, rate or integrity check failed" : error));
    }
    runs.push_back(Object({{"cycle", Number(cycle)},
                           {"started", Boolean(started)},
                           {"passed", Boolean(ok)},
                           {"width", Number(width)},
                           {"height", Number(height)},
                           {"dimensions_match", Boolean(dimensions_match)},
                           {"frames", Number(stats.frames)},
                           {"sensor_fps", Number(stats.fps())},
                           {"stopped", Boolean(stopped)},
                           {"first_timestamp_ns", Number(stats.first_timestamp)},
                           {"last_timestamp_ns", Number(stats.previous_timestamp)},
                           {"last_sequence", Number(stats.previous_sequence)},
                           {"timestamp_flags", Number(timestamp_flags)},
                           {"sequence_gaps", Number(stats.sequence_gaps)},
                           {"duplicates", Number(stats.duplicates)},
                           {"sequence_regressions", Number(stats.regressions)},
                           {"timestamp_errors", Number(stats.timestamp_errors)},
                           {"buffer_errors", Number(stats.buffer_errors)},
                           {"timeouts", Number(timeouts)},
                           {"not_ready", Number(not_ready)},
                           {"io_errors", Number(io_errors)},
                           {"error", Quote(error)},
                           {"wall_ms", Number(std::chrono::duration<double, std::milli>(
                                                  std::chrono::steady_clock::now() - start)
                                                  .count())}}));
  }
  return Object({{"backend", Quote("v4l2_mmap_raw10")},
                 {"passed", Boolean(*passed)},
                 {"requested_width", Number(options.width)},
                 {"requested_height", Number(options.height)},
                 {"requested_fps", Number(options.fps)},
                 {"frames_per_cycle", Number(options.frames)},
                 {"cycles", Array(runs)}});
}
}  // namespace cockpit::camera_diag
