#pragma once

#include <QImage>
#include <QObject>
#include <QString>

namespace cockpit {
namespace ui {

class CameraImageProvider;

class CameraFrameModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
  Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY frameChanged)
  Q_PROPERTY(QString frameSource READ frameSource NOTIFY frameChanged)
  Q_PROPERTY(quint64 sequence READ sequence NOTIFY frameChanged)
  Q_PROPERTY(int frameWidth READ frameWidth NOTIFY frameChanged)
  Q_PROPERTY(int frameHeight READ frameHeight NOTIFY frameChanged)

 public:
  explicit CameraFrameModel(CameraImageProvider* image_provider, QObject* parent = nullptr);

  bool connected() const {
    return connected_;
  }
  bool hasFrame() const {
    return has_frame_;
  }
  const QString& frameSource() const {
    return frame_source_;
  }
  quint64 sequence() const {
    return sequence_;
  }
  int frameWidth() const {
    return frame_width_;
  }
  int frameHeight() const {
    return frame_height_;
  }

  void SetConnected(bool connected);
  void UpdateFrame(QImage image, quint64 sequence, quint64 generation);

 signals:
  void connectedChanged();
  void frameChanged();

 private:
  CameraImageProvider* image_provider_;
  bool connected_ = false;
  bool has_frame_ = false;
  QString frame_source_;
  quint64 sequence_ = 0;
  int frame_width_ = 0;
  int frame_height_ = 0;
};

}  // namespace ui
}  // namespace cockpit
