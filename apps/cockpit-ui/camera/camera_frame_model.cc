#include "camera_frame_model.h"

#include <utility>

#include "camera_image_provider.h"

namespace cockpit {
namespace ui {

CameraFrameModel::CameraFrameModel(CameraImageProvider* image_provider, QObject* parent,
                                   int stale_timeout_ms)
    : QObject(parent), image_provider_(image_provider) {
  stale_timer_.setSingleShot(true);
  stale_timer_.setInterval(stale_timeout_ms);
  connect(&stale_timer_, &QTimer::timeout, this, [this] {
    SetFresh(false);
  });
}

void CameraFrameModel::SetConnected(bool connected) {
  if (connected_ == connected) {
    return;
  }
  connected_ = connected;
  if (!connected_) {
    stale_timer_.stop();
    SetFresh(false);
  }
  emit connectedChanged();
}

void CameraFrameModel::UpdateFrame(QImage image, quint64 sequence, quint64 generation) {
  if (image_provider_ == nullptr || image.isNull()) {
    return;
  }
  frame_width_ = image.width();
  frame_height_ = image.height();
  sequence_ = sequence;
  image_provider_->SetImage(std::move(image));
  frame_source_ = QStringLiteral("image://camera/frame?generation=%1").arg(generation);
  has_frame_ = true;
  SetFresh(true);
  stale_timer_.start();
  emit frameChanged();
}

void CameraFrameModel::SetFresh(bool fresh) {
  if (fresh_ == fresh) {
    return;
  }
  fresh_ = fresh;
  emit freshnessChanged();
}

}  // namespace ui
}  // namespace cockpit
