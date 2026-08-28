#include "cockpit/modules/camera/capture/v4l2_preview_source.h"

#include <algorithm>
#include <chrono>
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
  }
  stop_requested_.store(false);
  running_.store(true);
  worker_ = std::thread(&V4l2PreviewSource::Run, this);
  return true;
}

void V4l2PreviewSource::Stop() {
  stop_requested_.store(true);
  if (worker_.joinable()) worker_.join();
  std::lock_guard<std::mutex> lock(mutex_);
  capture_.reset();
  callback_ = nullptr;
  running_.store(false);
}

void V4l2PreviewSource::Run() {
  while (!stop_requested_.load()) {
    V4l2RawFrame raw;
    std::string error;
    V4l2MmapCapture* capture = nullptr;
    FrameCallback callback;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      capture = capture_.get();
      callback = callback_;
    }
    if (capture == nullptr || !callback) break;
    if (!capture->WaitFrame(&raw, 1000, &error)) {
      if (stop_requested_.load()) break;
      continue;
    }
    CameraFrame frame;
    frame.sequence = raw.sequence;
    frame.timestamp_ms = raw.timestamp_ns > 0
                             ? static_cast<std::uint64_t>(raw.timestamp_ns / 1000000LL)
                             : static_cast<std::uint64_t>(time::NowMs());
    frame.width = raw.width;
    frame.height = raw.height;
    frame.stride_bytes = raw.bytes_per_line;
    frame.format = CameraPixelFormat::kSrgGb10;
    frame.data = std::move(raw.data);
    callback(std::move(frame));
  }
  running_.store(false);
}

}  // namespace cockpit::camera
