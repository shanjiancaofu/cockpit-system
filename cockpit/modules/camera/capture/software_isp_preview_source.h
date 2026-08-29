#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "cockpit/drivers/v4l2/v4l2_mmap_capture.h"
#include "cockpit/modules/camera/capture/camera_preview_source.h"
#include "cockpit/modules/camera/isp/software_isp.h"

namespace cockpit::camera {

struct SoftwareIspPreviewStats {
  std::uint64_t captured_frames = 0;
  std::uint64_t processed_frames = 0;
  std::uint64_t dropped_queue_frames = 0;
  std::uint64_t source_sequence_gaps = 0;
  double queue_to_output_mean_ms = 0.0;
  double queue_to_output_p50_ms = 0.0;
  double queue_to_output_p95_ms = 0.0;
  double queue_to_output_max_ms = 0.0;
  SoftwareIspTimingMs isp_mean_ms;
};

class SoftwareIspPreviewSource final : public CameraPreviewSource {
 public:
  SoftwareIspPreviewSource() = default;
  ~SoftwareIspPreviewSource() override;

  bool Start(const CameraPreviewConfig& config, FrameCallback callback,
             std::string* error) override;
  void Stop() override;
  bool IsRunning() const override {
    return running_.load();
  }
  SoftwareIspPreviewStats stats() const;

 private:
  struct PendingFrame {
    RawBayerFrame frame;
    std::chrono::steady_clock::time_point enqueued_at;
  };

  void CaptureLoop();
  void IspLoop();
  void ResetStats();

  std::unique_ptr<V4l2MmapCapture> capture_;
  SoftwareIsp isp_;
  CameraPreviewConfig config_;
  FrameCallback callback_;
  mutable std::mutex mutex_;
  std::condition_variable queue_condition_;
  std::deque<PendingFrame> queue_;
  std::deque<double> latency_samples_ms_;
  SoftwareIspPreviewStats stats_;
  std::atomic_bool stop_requested_{false};
  std::atomic_bool running_{false};
  std::thread capture_worker_;
  std::thread isp_worker_;
};

}  // namespace cockpit::camera
