#include "camera_preview_probe.h"

#include <sys/resource.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>

#include "cockpit/core/runtime/process_runtime.h"
#include "cockpit/modules/camera/capture/argus_isp_preview_source.h"
#include "cockpit/modules/camera/capture/uvc_preview_source.h"
#if defined(COCKPIT_CAMERA_HAS_SOFTWARE_ISP)
#include "cockpit/modules/camera/capture/software_isp_preview_source.h"
#endif

namespace {

void PrintUsage() {
  std::cout << "camera-preview-probe --backend argus|uvc|software_isp --device DEVICE "
               "[--uvc-input-format mjpeg|yuyv] [--frames 30] "
               "[--width 640] [--height 480] [--fps 30] "
               "[--timeout-ms 5000] [--config configs/development.yaml]\n";
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

int cockpit::camera_preview_probe::ProbeCameraPreview(
    const cockpit::runtime::ProcessRuntime& runtime) {
  if (runtime.args().HasFlag("help")) {
    PrintUsage();
    return 0;
  }

  const std::string backend = runtime.args().GetString("backend", "argus");
  cockpit::camera::CameraPreviewConfig config;
  config.device =
      runtime.args().GetString("device", backend == "argus" ? "nvargus://0" : "/dev/video0");
  config.width = static_cast<std::uint32_t>(runtime.args().GetInt("width", 640));
  config.height = static_cast<std::uint32_t>(runtime.args().GetInt("height", 480));
  config.fps = static_cast<std::uint32_t>(runtime.args().GetInt("fps", 30));

  const int target_frames = runtime.args().GetInt("frames", 30);
  const int timeout_ms = runtime.args().GetInt("timeout-ms", 5000);
  if (target_frames <= 0 || timeout_ms <= 0) {
    std::cerr << "frames and timeout-ms must be positive\n";
    return 2;
  }

  ProbeState state;
  std::unique_ptr<cockpit::camera::CameraPreviewSource> preview;
#if defined(COCKPIT_CAMERA_HAS_SOFTWARE_ISP)
  if (backend == "software_isp") {
    preview = std::make_unique<cockpit::camera::SoftwareIspPreviewSource>();
  } else
#endif
      if (backend == "argus") {
    preview = std::make_unique<cockpit::camera::ArgusIspPreviewSource>();
  } else if (backend == "uvc") {
    const std::string input_format = runtime.args().GetString("uvc-input-format", "mjpeg");
    cockpit::camera::CameraUvcInputFormat parsed_format;
    if (input_format == "mjpeg") {
      parsed_format = cockpit::camera::CameraUvcInputFormat::kMjpeg;
    } else if (input_format == "yuyv") {
      parsed_format = cockpit::camera::CameraUvcInputFormat::kYuyv;
    } else {
      std::cerr << "uvc-input-format must be mjpeg or yuyv\n";
      return 2;
    }
    preview = std::make_unique<cockpit::camera::UvcPreviewSource>(parsed_format);
  } else {
    std::cerr << "backend must be argus, uvc, or software_isp\n";
    return 2;
  }
  std::string error;
  const auto wall_started = std::chrono::steady_clock::now();
  rusage usage_started{};
  getrusage(RUSAGE_SELF, &usage_started);
#if defined(COCKPIT_CAMERA_HAS_SOFTWARE_ISP)
  auto* software_isp_preview =
      dynamic_cast<cockpit::camera::SoftwareIspPreviewSource*>(preview.get());
#else
  void* software_isp_preview = nullptr;
#endif
  const bool started = preview->Start(
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
    return 1;
  }

  bool complete = false;
  {
    std::unique_lock<std::mutex> lock(state.mutex);
    complete = state.condition.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                        [&state, target_frames] {
                                          return state.frames >= target_frames;
                                        });
  }
  preview->Stop();
  const auto wall_elapsed = std::chrono::steady_clock::now() - wall_started;
  rusage usage_finished{};
  getrusage(RUSAGE_SELF, &usage_finished);

  std::lock_guard<std::mutex> lock(state.mutex);
  if (state.frames == 0) {
    std::cerr << "no preview frames received from " << config.device << '\n';
    return 1;
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
#if defined(COCKPIT_CAMERA_HAS_SOFTWARE_ISP)
  if (software_isp_preview != nullptr) {
    const auto stats = software_isp_preview->stats();
    const auto cpu_time = [](const rusage& usage) {
      return static_cast<double>(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1000.0 +
             static_cast<double>(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000.0;
    };
    const double wall_ms = std::chrono::duration<double, std::milli>(wall_elapsed).count();
    const double cpu_ms = cpu_time(usage_finished) - cpu_time(usage_started);
    std::cout << "software_isp_captured_frames=" << stats.captured_frames
              << " processed_frames=" << stats.processed_frames
              << " queue_dropped_frames=" << stats.dropped_queue_frames
              << " source_sequence_gaps=" << stats.source_sequence_gaps << '\n'
              << "queue_to_output_mean_ms=" << stats.queue_to_output_mean_ms
              << " p50_ms=" << stats.queue_to_output_p50_ms
              << " p95_ms=" << stats.queue_to_output_p95_ms
              << " max_ms=" << stats.queue_to_output_max_ms << '\n'
              << "isp_mean_raw_unpack_ms=" << stats.isp_mean_ms.raw_unpack
              << " normalize_ms=" << stats.isp_mean_ms.normalize
              << " demosaic_ms=" << stats.isp_mean_ms.demosaic
              << " gain_gamma_ms=" << stats.isp_mean_ms.gain_gamma
              << " output_ms=" << stats.isp_mean_ms.output
              << " total_ms=" << stats.isp_mean_ms.total << '\n'
              << "process_cpu_ms=" << cpu_ms << " process_wall_ms=" << wall_ms
              << " process_cpu_percent=" << (wall_ms > 0.0 ? cpu_ms / wall_ms * 100.0 : 0.0)
              << " max_rss_kib=" << usage_finished.ru_maxrss << '\n';
  }
#endif
  if (!complete) {
    std::cerr << "timed out before requested frame count\n";
    return 1;
  }
  return 0;
}
