#include "cockpit/modules/camera/capture/v4l2_preview_source.h"

#include <linux/videodev2.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <utility>

#include "cockpit/core/time/time.h"

namespace cockpit::camera {

V4l2PreviewSource::~V4l2PreviewSource() {
  Stop();
}

bool V4l2PreviewSource::Start(const CameraPreviewConfig& config, FrameCallback callback,
                              std::string* error) {
  Stop();
  if (!callback || config.device.rfind("/dev/video", 0) != 0 || config.width == 0 ||
      config.height == 0 || config.fps == 0) {
    if (error != nullptr) *error = "invalid V4L2 preview configuration";
    return false;
  }
  auto capture = std::make_unique<V4l2MmapCapture>();
  V4l2MmapConfig capture_config;
  capture_config.device = config.device;
  capture_config.width = config.width;
  capture_config.height = config.height;
  capture_config.fps = config.fps;
  if (!capture->Start(capture_config, error)) return false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    callback_ = std::move(callback);
    capture_ = std::move(capture);
    ResetStats();
  }
  stop_requested_.store(false);
  running_.store(true);
  capture_worker_ = std::thread(&V4l2PreviewSource::CaptureLoop, this);
  isp_worker_ = std::thread(&V4l2PreviewSource::IspLoop, this);
  return true;
}

void V4l2PreviewSource::Stop() {
  stop_requested_.store(true);
  queue_condition_.notify_all();
  if (capture_worker_.joinable()) capture_worker_.join();
  if (isp_worker_.joinable()) isp_worker_.join();
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.clear();
  capture_.reset();
  callback_ = nullptr;
  running_.store(false);
}

void V4l2PreviewSource::ResetStats() {
  queue_.clear();
  latency_samples_ms_.clear();
  stats_ = {};
}

void V4l2PreviewSource::CaptureLoop() {
  std::uint32_t previous_sequence = 0;
  bool have_sequence = false;
  while (!stop_requested_.load()) {
    V4l2RawFrame raw;
    std::string error;
    V4l2MmapCapture* capture = nullptr;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      capture = capture_.get();
    }
    if (capture == nullptr) break;
    if (!capture->WaitFrame(&raw, 1000, &error)) {
      if (stop_requested_.load()) break;
      continue;
    }
    const auto enqueued_at = std::chrono::steady_clock::now();
    const auto received_at_ns = time::NowNs();
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.captured_frames;
    if (have_sequence && raw.sequence > previous_sequence + 1U) {
      stats_.source_sequence_gaps += raw.sequence - previous_sequence - 1U;
    }
    previous_sequence = raw.sequence;
    have_sequence = true;
    if (queue_.size() >= 2U) {
      queue_.pop_front();
      ++stats_.dropped_queue_frames;
    }
    PendingFrame pending;
    pending.frame.width = raw.width;
    pending.frame.height = raw.height;
    pending.frame.bytes_per_line = raw.bytes_per_line;
    pending.frame.bytes_used = raw.bytes_used;
    pending.frame.sequence = raw.sequence;
    pending.frame.timestamp_ms = static_cast<std::uint64_t>(received_at_ns / 1000000LL);
    pending.frame.received_at_ns = received_at_ns;
    pending.frame.source_timestamp_ns = raw.timestamp_ns;
    pending.frame.source_timestamp_flags = raw.timestamp_flags;
    pending.frame.source_timestamp_valid = raw.timestamp_ns > 0;
    if ((raw.timestamp_flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) == V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC) {
      pending.frame.source_clock = CameraTimestampClock::kMonotonic;
    }
    pending.frame.data = std::move(raw.data);
    pending.enqueued_at = enqueued_at;
    queue_.push_back(std::move(pending));
    queue_condition_.notify_one();
  }
}

void V4l2PreviewSource::IspLoop() {
  while (!stop_requested_.load()) {
    PendingFrame pending;
    FrameCallback callback;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      queue_condition_.wait(lock, [this] {
        return stop_requested_.load() || !queue_.empty();
      });
      if (queue_.empty()) continue;
      pending = std::move(queue_.front());
      queue_.pop_front();
      callback = callback_;
    }
    if (!callback) break;
    CameraFrame frame;
    RawBayerFrame raw = std::move(pending.frame);
    SoftwareIspTimingMs timing;
    std::string error;
    if (!isp_.Process(raw, &frame, &error, &timing)) continue;
    const double latency = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - pending.enqueued_at)
                               .count();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      ++stats_.processed_frames;
      latency_samples_ms_.push_back(latency);
      constexpr std::size_t kLatencyWindow = 2048U;
      if (latency_samples_ms_.size() > kLatencyWindow) {
        latency_samples_ms_.pop_front();
      }
      const auto add_mean = [](double current, double value, std::uint64_t count) {
        return current + (value - current) / static_cast<double>(count);
      };
      const auto count = stats_.processed_frames;
      stats_.isp_mean_ms.raw_unpack =
          add_mean(stats_.isp_mean_ms.raw_unpack, timing.raw_unpack, count);
      stats_.isp_mean_ms.normalize =
          add_mean(stats_.isp_mean_ms.normalize, timing.normalize, count);
      stats_.isp_mean_ms.demosaic = add_mean(stats_.isp_mean_ms.demosaic, timing.demosaic, count);
      stats_.isp_mean_ms.color_correction =
          add_mean(stats_.isp_mean_ms.color_correction, timing.color_correction, count);
      stats_.isp_mean_ms.output = add_mean(stats_.isp_mean_ms.output, timing.output, count);
      stats_.isp_mean_ms.total = add_mean(stats_.isp_mean_ms.total, timing.total, count);
    }
    callback(std::move(frame));
  }
  running_.store(false);
}

V4l2PreviewStats V4l2PreviewSource::stats() const {
  std::lock_guard<std::mutex> lock(mutex_);
  V4l2PreviewStats result = stats_;
  if (!latency_samples_ms_.empty()) {
    std::vector<double> samples(latency_samples_ms_.begin(), latency_samples_ms_.end());
    std::sort(samples.begin(), samples.end());
    const auto percentile = [&samples](double fraction) {
      const auto index =
          static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(samples.size())));
      return samples[std::max<std::size_t>(1U, index) - 1U];
    };
    result.queue_to_output_mean_ms =
        std::accumulate(samples.begin(), samples.end(), 0.0) / static_cast<double>(samples.size());
    result.queue_to_output_p50_ms = percentile(0.50);
    result.queue_to_output_p95_ms = percentile(0.95);
    result.queue_to_output_max_ms = samples.back();
  }
  return result;
}

}  // namespace cockpit::camera
