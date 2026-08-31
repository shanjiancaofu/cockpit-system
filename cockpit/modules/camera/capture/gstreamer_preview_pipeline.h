#pragma once

#include <gst/gst.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "cockpit/core/base/macros.h"
#include "cockpit/modules/camera/capture/camera_preview_source.h"

namespace cockpit {
namespace camera {

enum class GstreamerCameraSource {
  kArgusIsp,
  kUvc,
};

class GstreamerPreviewPipeline : public CameraPreviewSource {
 public:
  ~GstreamerPreviewPipeline() override;

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(GstreamerPreviewPipeline);

  bool Start(const CameraPreviewConfig& config, FrameCallback callback,
             std::string* error) override;
  void Stop() override;
  bool IsRunning() const override {
    return running_.load();
  }

 private:
  friend class ArgusIspPreviewSource;
  friend class UvcPreviewSource;

  explicit GstreamerPreviewPipeline(
      GstreamerCameraSource source,
      CameraUvcInputFormat uvc_input_format = CameraUvcInputFormat::kMjpeg);
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
  const GstreamerCameraSource source_;
  const CameraUvcInputFormat uvc_input_format_;
  std::atomic_bool running_{false};
  std::uint64_t sequence_ = 0;
};

}  // namespace camera
}  // namespace cockpit
