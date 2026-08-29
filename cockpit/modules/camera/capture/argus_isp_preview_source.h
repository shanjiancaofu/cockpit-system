#pragma once

#include <utility>

#include "cockpit/modules/camera/capture/gstreamer_preview_pipeline.h"

namespace cockpit::camera {

class ArgusIspPreviewSource final : public CameraPreviewSource {
 public:
  ArgusIspPreviewSource() : pipeline_(GstreamerCameraSource::kArgusIsp) {
  }

  bool Start(const CameraPreviewConfig& config, FrameCallback callback,
             std::string* error) override {
    return pipeline_.Start(config, std::move(callback), error);
  }
  void Stop() override {
    pipeline_.Stop();
  }
  bool IsRunning() const override {
    return pipeline_.IsRunning();
  }

 private:
  GstreamerPreviewPipeline pipeline_;
};

}  // namespace cockpit::camera
