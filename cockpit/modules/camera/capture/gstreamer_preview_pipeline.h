#pragma once

#include <gst/gst.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "cockpit/modules/camera/capture/camera_preview_source.h"

namespace cockpit {
namespace camera {

class GstreamerPreviewPipeline : public CameraPreviewSource {
 public:
  GstreamerPreviewPipeline();
  ~GstreamerPreviewPipeline() override;

  GstreamerPreviewPipeline(const GstreamerPreviewPipeline&) = delete;
  GstreamerPreviewPipeline& operator=(const GstreamerPreviewPipeline&) = delete;

  bool Start(const CameraPreviewConfig& config, FrameCallback callback,
             std::string* error) override;
  void Stop() override;
  bool IsRunning() const override {
    return running_.load();
  }

 private:
  static void EnsureGstreamerInitialized();
  static int OnNewSample(void* appsink, void* user_data);

  void ReleasePipeline();
  int HandleNewSample(GstSample* sample);
  std::string BuildPipelineDescription(const CameraPreviewConfig& config) const;

  mutable std::mutex mutex_;
  GstElement* pipeline_ = nullptr;
  GstElement* appsink_ = nullptr;
  FrameCallback callback_;
  CameraPreviewConfig config_;
  std::atomic_bool running_{false};
  std::uint64_t sequence_ = 0;
};

}  // namespace camera
}  // namespace cockpit
