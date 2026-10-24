#include <sys/resource.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "cockpit/driver/camera/capture/native_argus_capture.h"
#include "tools/camera/camera-diag/diagnostics.h"

namespace {
namespace diag = cockpit::camera_diag;
namespace camera = cockpit::camera;
void Require(bool ok, const std::string& message) {
  if (!ok) throw std::runtime_error(message);
}
diag::Json Run(camera::NativeArgusCapture& capture, const camera::NativeCaptureConfig& config,
               const diag::Options& options, int cycle, std::ofstream& metadata, bool* passed) {
  diag::FrameStats stats;
  std::string error;
  std::uint64_t luma_sample_sum = 0;
  const auto before = capture.stats();
  const auto started = std::chrono::steady_clock::now();
  int mode = -1;
  try {
    Require(capture.Start(config, &error), error);
    mode = capture.sensor_mode();
    for (int i = 0; i < options.frames; ++i) {
      camera::FrameLease lease;
      const auto status = capture.Acquire(&lease, options.timeout_ms, &error);
      Require(status == camera::NativeAcquireStatus::kFrame, error);
      const auto& frame = lease.frame();
      stats.Observe(frame.capture_id, static_cast<std::int64_t>(frame.sensor_timestamp_ns), false);
      if (i == 0) Require(capture.SampleLuma(lease, 64, &luma_sample_sum, &error), error);
      metadata << cycle << '\t' << i << '\t' << frame.capture_id << '\t'
               << frame.sensor_timestamp_ns << '\t' << frame.frame_duration_ns << '\t'
               << frame.dmabuf_fd << '\n';
      Require(static_cast<bool>(metadata), "write metadata");
      // Lease destruction queues the return. The next Acquire/Stop flushes it to Argus.
    }
  } catch (const std::exception& exception) {
    error = exception.what();
  }
  std::string stop_error;
  const auto stop = capture.Stop(&stop_error);
  if (stop != camera::NativeStopStatus::kStopped) error += " stop: " + stop_error;
  const auto after = capture.stats();
  const bool ok = error.empty() && after.ResourcesBalanced() && stats.healthy() &&
                  stats.frames == static_cast<std::uint64_t>(options.frames) &&
                  stats.fps() >= options.fps * 0.95 && stats.fps() <= options.fps * 1.05;
  *passed = *passed && ok;
  return diag::Object(
      {{"cycle", diag::Number(cycle)},
       {"passed", diag::Boolean(ok)},
       {"sensor_mode", diag::Number(mode)},
       {"frames", diag::Number(stats.frames)},
       {"sensor_fps", diag::Number(stats.fps())},
       {"capture_id_gaps", diag::Number(stats.sequence_gaps)},
       {"capture_id_duplicates", diag::Number(stats.duplicates)},
       {"capture_id_regressions", diag::Number(stats.regressions)},
       {"timestamp_errors", diag::Number(stats.timestamp_errors)},
       {"timeouts", diag::Number(after.timeouts - before.timeouts)},
       {"first_timestamp_ns", diag::Number(stats.first_timestamp)},
       {"last_timestamp_ns", diag::Number(stats.previous_timestamp)},
       {"imported_fds", diag::Number(after.imported_fds - before.imported_fds)},
       {"frames_acquired", diag::Number(after.frames_acquired - before.frames_acquired)},
       {"leases_returned", diag::Number(after.leases_returned - before.leases_returned)},
       {"buffers_released", diag::Number(after.buffers_released - before.buffers_released)},
       {"outstanding_leases", diag::Number(after.outstanding_leases)},
       {"resources_balanced", diag::Boolean(after.ResourcesBalanced())},
       {"luma_sample_sum", diag::Number(luma_sample_sum)},
       {"error", diag::Quote(error)},
       {"wall_ms", diag::Number(std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - started)
                                    .count())}});
}
}  // namespace

int main(int argc, char** argv) {
  diag::Options options;
  options.device = "0";
  options.output = "libargus_capture.json";
  std::string error;
  if (!diag::ParseOptions(argc, argv, &options, &error)) {
    std::cerr << error << '\n';
    return 2;
  }
  if (options.help) {
    std::cout << "libargus-capture-probe [--device 0] [--width 1920] [--height 1080] [--fps 30]"
                 " [--frames 300] [--cycles 1] [--timeout-ms 5000] [--output report.json]\n";
    return 0;
  }
  if (options.inspect_only || options.frames < 2) {
    std::cerr << "capture requires at least two frames\n";
    return 2;
  }
  camera::NativeArgusCapture capture;
  camera::NativeCaptureConfig config;
  config.width = options.width;
  config.height = options.height;
  config.fps = options.fps;
  std::vector<diag::Json> runs;
  bool passed = true;
  std::string argus_version;
  std::ofstream metadata(options.output + ".frames.tsv");
  metadata << "cycle\tframe\tcapture_id\tsensor_timestamp_ns\tframe_duration_ns\tdmabuf_fd\n";
  const auto started = std::chrono::steady_clock::now();
  rusage before{};
  getrusage(RUSAGE_SELF, &before);
  try {
    Require(static_cast<bool>(metadata), "open metadata output");
    std::size_t end = 0;
    const auto index = std::stoul(options.device, &end);
    Require(end == options.device.size() && options.device.front() != '-',
            "device must be a nonnegative Argus index");
    Require(index <= UINT32_MAX, "Argus device index out of range");
    config.device_index = static_cast<std::uint32_t>(index);
    for (int cycle = 0; cycle < options.cycles; ++cycle) {
      runs.push_back(Run(capture, config, options, cycle, metadata, &passed));
      if (capture.stats().native_resources_retained_until_exit) break;
    }
    argus_version = capture.version();
  } catch (const std::exception& exception) {
    error = exception.what();
    passed = false;
  }
  metadata.close();
  const auto lifecycle = capture.stats();
  const bool balanced = lifecycle.ResourcesBalanced();
  passed = passed && balanced && static_cast<bool>(metadata);
  rusage after{};
  getrusage(RUSAGE_SELF, &after);
  const auto cpu_ms = [](const rusage& value) {
    return (value.ru_utime.tv_sec + value.ru_stime.tv_sec) * 1000.0 +
           (value.ru_utime.tv_usec + value.ru_stime.tv_usec) / 1000.0;
  };
  const double wall_ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
  const auto report = diag::Object(
      {{"schema_version", "2"},
       {"implementation", diag::Quote("NativeArgusCapture + FrameLease")},
       {"frames_acquired", diag::Number(lifecycle.frames_acquired)},
       {"leases_returned", diag::Number(lifecycle.leases_returned)},
       {"buffers_released", diag::Number(lifecycle.buffers_released)},
       {"outstanding_leases", diag::Number(lifecycle.outstanding_leases)},
       {"pending_returns", diag::Number(lifecycle.pending_returns)},
       {"peak_leases", diag::Number(lifecycle.peak_leases)},
       {"release_errors", diag::Number(lifecycle.release_errors)},
       {"passed", diag::Boolean(passed)},
       {"environment", diag::InspectEnvironment()},
       {"argus_version", diag::Quote(argus_version)},
       {"stream_type", diag::Quote("STREAM_TYPE_BUFFER")},
       {"buffer_type", diag::Quote("NvBufSurface NV12 BLOCK_LINEAR -> EGLImage")},
       {"sync_type",
        diag::Quote(
            "SYNC_TYPE_NONE; acquire completes producer writes, CPU read finishes before release")},
       {"width", diag::Number(options.width)},
       {"height", diag::Number(options.height)},
       {"fps", diag::Number(options.fps)},
       {"runs", diag::Array(runs)},
       {"allocated", diag::Number(lifecycle.allocated)},
       {"destroyed", diag::Number(lifecycle.destroyed)},
       {"egl_mapped", diag::Number(lifecycle.egl_mapped)},
       {"egl_unmapped", diag::Number(lifecycle.egl_unmapped)},
       {"cpu_mapped", diag::Number(lifecycle.cpu_mapped)},
       {"cpu_unmapped", diag::Number(lifecycle.cpu_unmapped)},
       {"resources_balanced", diag::Boolean(balanced)},
       {"native_resources_retained_until_exit",
        diag::Boolean(lifecycle.native_resources_retained_until_exit)},
       {"cleanup_errors", diag::Strings(lifecycle.cleanup_errors)},
       {"process_wall_ms", diag::Number(wall_ms)},
       {"process_cpu_percent", diag::Number((cpu_ms(after) - cpu_ms(before)) / wall_ms * 100.0)},
       {"max_rss_kib", diag::Number(after.ru_maxrss)},
       {"error", diag::Quote(error)}});
  if (!diag::WriteReport(options.output, report)) {
    std::cerr << "cannot write report\n";
    return 2;
  }
  std::cout << "passed=" << passed << " report=" << options.output << " error=" << error << '\n';
  return passed ? 0 : 1;
}
