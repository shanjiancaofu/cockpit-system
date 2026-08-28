#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

#include "cockpit/drivers/v4l2/v4l2_mmap_capture.h"
#include "cockpit/modules/camera/capture/camera_preview_source.h"

namespace cockpit::camera {

class V4l2PreviewSource final : public CameraPreviewSource {
 public:
  V4l2PreviewSource() = default;
  ~V4l2PreviewSource() override;

  bool Start(const CameraPreviewConfig& config, FrameCallback callback,
             std::string* error) override;
  void Stop() override;
  bool IsRunning() const override {
    return running_.load();
  }

 private:
  void Run();

  std::unique_ptr<V4l2MmapCapture> capture_;
  CameraPreviewConfig config_;
  FrameCallback callback_;
  mutable std::mutex mutex_;
  std::atomic_bool stop_requested_{false};
  std::atomic_bool running_{false};
  std::thread worker_;
};

}  // namespace cockpit::camera
