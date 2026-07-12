#include "cockpit/modules/camera/capture/synthetic_preview_source.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>
#include <vector>

#include "cockpit/core/time/time.h"

namespace cockpit {
namespace camera {
namespace {

void AssignError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

}  // namespace

SyntheticPreviewSource::SyntheticPreviewSource(SyntheticCameraOptions options)
    : fault_(options.fault), fault_after_frames_(options.fault_after_frames) {
}

SyntheticPreviewSource::~SyntheticPreviewSource() {
  Stop();
}

bool SyntheticPreviewSource::Start(const CameraPreviewConfig& config, FrameCallback callback,
                                   std::string* error) {
  Stop();
  if (!callback || config.width == 0 || config.height == 0 || config.fps == 0 ||
      config.output_format != CameraPixelFormat::kBgrx) {
    AssignError(error, "invalid synthetic camera preview config");
    return false;
  }
  const std::uint64_t stride = static_cast<std::uint64_t>(config.width) * 4U;
  if (stride > std::numeric_limits<std::uint32_t>::max() ||
      stride * config.height > std::numeric_limits<std::size_t>::max()) {
    AssignError(error, "synthetic camera frame size is too large");
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    callback_ = std::move(callback);
  }
  stop_requested_.store(false);
  running_.store(true);
  worker_ = std::thread(&SyntheticPreviewSource::Run, this);
  return true;
}

void SyntheticPreviewSource::Stop() {
  stop_requested_.store(true);
  if (worker_.joinable()) {
    worker_.join();
  }
  running_.store(false);
  std::lock_guard<std::mutex> lock(mutex_);
  callback_ = nullptr;
}

bool SyntheticPreviewSource::IsRunning() const {
  return running_.load();
}

void SyntheticPreviewSource::SetFault(SyntheticCameraFault fault,
                                      std::uint64_t fault_after_frames) {
  fault_after_frames_.store(fault_after_frames);
  fault_.store(fault);
}

void SyntheticPreviewSource::Run() {
  CameraPreviewConfig config;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    config = config_;
  }
  const auto frame_interval = std::chrono::milliseconds(std::max(1U, 1000U / config.fps));
  const std::uint32_t stride = config.width * 4U;
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(stride) * config.height);
  std::uint64_t emitted_frames = 0;
  while (!stop_requested_.load()) {
    const SyntheticCameraFault fault = fault_.load();
    const bool fault_active = emitted_frames >= fault_after_frames_.load();
    if (fault == SyntheticCameraFault::kNoFrames ||
        (fault == SyntheticCameraFault::kStall && fault_active)) {
      std::this_thread::sleep_for(frame_interval);
      continue;
    }
    if (fault == SyntheticCameraFault::kDisconnect && fault_active) {
      running_.store(false);
      return;
    }

    CameraFrame frame;
    frame.sequence = ++emitted_frames;
    frame.timestamp_ms = static_cast<std::uint64_t>(time::NowMs());
    frame.width = config.width;
    frame.height = config.height;
    frame.stride_bytes = stride;
    frame.format = CameraPixelFormat::kBgrx;
    frame.data = pixels;
    FrameCallback callback;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      callback = callback_;
    }
    if (callback) {
      callback(std::move(frame));
    }
    std::this_thread::sleep_for(frame_interval);
  }
  running_.store(false);
}

SyntheticCameraFault ParseSyntheticCameraFault(const std::string& value) {
  if (value == "no_frames") {
    return SyntheticCameraFault::kNoFrames;
  }
  if (value == "stall") {
    return SyntheticCameraFault::kStall;
  }
  if (value == "disconnect") {
    return SyntheticCameraFault::kDisconnect;
  }
  return SyntheticCameraFault::kNone;
}

const char* ToString(SyntheticCameraFault fault) {
  switch (fault) {
    case SyntheticCameraFault::kNone:
      return "none";
    case SyntheticCameraFault::kNoFrames:
      return "no_frames";
    case SyntheticCameraFault::kStall:
      return "stall";
    case SyntheticCameraFault::kDisconnect:
      return "disconnect";
  }
  return "none";
}

}  // namespace camera
}  // namespace cockpit
