#include "services/camera-service/preview/camera_preview_module.h"

#include <memory>
#include <string>
#include <utility>

namespace cockpit {
namespace camera {

CameraPreviewModule::CameraPreviewModule(std::unique_ptr<CameraPreviewSource> preview_source)
    : BasicModule("camera-preview"), preview_source_(std::move(preview_source)) {
}

bool CameraPreviewModule::available() const {
  return preview_source_ != nullptr;
}

bool CameraPreviewModule::is_running() const {
  return preview_source_ != nullptr && preview_source_->IsRunning();
}

const std::string& CameraPreviewModule::last_error() const {
  return last_error_;
}

void CameraPreviewModule::Configure(CameraPreviewConfig config, FrameCallback callback) {
  config_ = std::move(config);
  callback_ = std::move(callback);
  last_error_.clear();
}

bool CameraPreviewModule::OnStart() {
  if (preview_source_ == nullptr) {
    last_error_ = "camera preview backend is not available";
    return false;
  }
  if (!callback_) {
    last_error_ = "camera preview callback is not configured";
    return false;
  }

  std::string error;
  if (!preview_source_->Start(config_, callback_, &error)) {
    last_error_ = error.empty() ? "start camera preview backend failed" : error;
    return false;
  }

  last_error_.clear();
  return true;
}

void CameraPreviewModule::OnStop() {
  if (preview_source_ != nullptr) {
    preview_source_->Stop();
  }
}

}  // namespace camera
}  // namespace cockpit
