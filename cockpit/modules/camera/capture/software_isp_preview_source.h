#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "cockpit/drivers/v4l2/v4l2_mmap_capture.h"
#include "cockpit/modules/camera/capture/camera_preview_source.h"
#include "cockpit/modules/camera/isp/software_isp.h"

namespace cockpit::camera {

class SoftwareIspCapture {
 public:
  virtual ~SoftwareIspCapture() = default;
  virtual bool Start(const V4l2MmapConfig& config, std::string* error) = 0;
  virtual bool WaitFrame(V4l2RawFrame* frame, int timeout_ms, std::string* error) = 0;
  virtual void Stop() = 0;
  virtual bool running() const = 0;
};

class SoftwareIspProcessor {
 public:
  virtual ~SoftwareIspProcessor() = default;
  virtual bool Process(const RawBayerFrame& raw, CameraFrame* output, std::string* error,
                       SoftwareIspTimingMs* timing) = 0;
};

struct SoftwareIspPreviewStats {
  std::uint64_t captured_frames = 0;
  std::uint64_t processed_frames = 0;
  std::uint64_t dropped_queue_frames = 0;
  std::uint64_t source_sequence_gaps = 0;
  std::uint64_t fatal_capture_errors = 0;
  std::uint64_t reconnect_attempts = 0;
  std::uint64_t reconnect_successes = 0;
  std::uint32_t consecutive_failures = 0;
  std::uint32_t last_reconnect_backoff_ms = 0;
  std::string last_error;
  double queue_to_output_mean_ms = 0.0;
  double queue_to_output_p50_ms = 0.0;
  double queue_to_output_p95_ms = 0.0;
  double queue_to_output_max_ms = 0.0;
  SoftwareIspTimingMs isp_mean_ms;
};

class SoftwareIspPreviewSource final : public CameraPreviewSource {
 public:
  using CaptureFactory = std::function<std::unique_ptr<SoftwareIspCapture>()>;

  SoftwareIspPreviewSource();
  explicit SoftwareIspPreviewSource(CaptureFactory capture_factory);
  SoftwareIspPreviewSource(CaptureFactory capture_factory,
                           std::unique_ptr<SoftwareIspProcessor> isp);
  ~SoftwareIspPreviewSource() override;

  bool Start(const CameraPreviewConfig& config, FrameCallback callback,
             std::string* error) override;
  void Stop() override;
  bool IsRunning() const override {
    return running_.load();
  }
  bool IsRecovering() const override {
    return recovering_.load();
  }
  SoftwareIspPreviewStats stats() const;

 private:
  struct PendingFrame {
    RawBayerFrame frame;
    std::uint64_t generation = 0;
    std::chrono::steady_clock::time_point enqueued_at;
  };

  void CaptureLoop();
  void IspLoop();
  void ResetStats();

  CaptureFactory capture_factory_;
  std::unique_ptr<SoftwareIspCapture> capture_;
  std::unique_ptr<SoftwareIspProcessor> isp_;
  CameraPreviewConfig config_;
  FrameCallback callback_;
  mutable std::mutex mutex_;
  std::condition_variable queue_condition_;
  std::deque<PendingFrame> queue_;
  std::deque<double> latency_samples_ms_;
  SoftwareIspPreviewStats stats_;
  std::atomic_bool stop_requested_{false};
  std::atomic_bool running_{false};
  std::atomic_bool recovering_{false};
  std::atomic_bool recovery_output_timeout_{false};
  std::atomic_uint32_t consecutive_failures_{0};
  std::uint64_t capture_generation_ = 0;
  std::chrono::steady_clock::time_point recovery_deadline_{};
  std::thread capture_worker_;
  std::thread isp_worker_;
};

}  // namespace cockpit::camera
