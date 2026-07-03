#pragma once

#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

namespace cockpit {
namespace ui {

class CameraImageProvider final : public QQuickImageProvider {
 public:
  CameraImageProvider();

  QImage requestImage(const QString& id, QSize* size, const QSize& requested_size) override;
  void SetImage(QImage image);

 private:
  QMutex mutex_;
  QImage image_;
};

}  // namespace ui
}  // namespace cockpit
