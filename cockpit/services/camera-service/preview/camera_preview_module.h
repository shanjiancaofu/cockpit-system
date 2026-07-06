#pragma once

#include <functional>
#include <memory>
#include <string>

#include "cockpit/core/runtime/Module.h"
#include "cockpit/modules/camera/capture/camera_preview_source.h"
#include "cockpit/modules/camera/frames/camera_frame.h"

namespace cockpit {
namespace camera {

class CameraPreviewModule : public runtime::BasicModule {
 public:
  using FrameCallback = CameraPreviewSource::FrameCallback;

  explicit CameraPreviewModule(std::unique_ptr<CameraPreviewSource> preview_source);
  ~CameraPreviewModule() override = default;

  CameraPreviewModule(const CameraPreviewModule&) = delete;
  CameraPreviewModule& operator=(const CameraPreviewModule&) = delete;

  bool available() const;
  bool is_running() const;
  const std::string& last_error() const;

  void Configure(CameraPreviewConfig config, FrameCallback callback);

 protected:
  bool OnStart() override;
  void OnStop() override;

 private:
  std::unique_ptr<CameraPreviewSource> preview_source_;
  CameraPreviewConfig config_;
  FrameCallback callback_;
  std::string last_error_;
};

}  // namespace camera
}  // namespace cockpit
