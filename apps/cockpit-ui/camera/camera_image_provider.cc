#include "camera_image_provider.h"

#include <QMutexLocker>
#include <utility>

namespace cockpit {
namespace ui {

CameraImageProvider::CameraImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {
}

QImage CameraImageProvider::requestImage(const QString&, QSize* size, const QSize& requested_size) {
  QMutexLocker lock(&mutex_);
  if (size != nullptr) {
    *size = image_.size();
  }
  if (requested_size.isValid() && !image_.isNull()) {
    return image_.scaled(requested_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  }
  return image_;
}

void CameraImageProvider::SetImage(QImage image) {
  QMutexLocker lock(&mutex_);
  image_ = std::move(image);
}

}  // namespace ui
}  // namespace cockpit
