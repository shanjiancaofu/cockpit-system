#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace cockpit {
namespace ui {

class CameraControlModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QStringList devices READ devices NOTIFY devicesChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY statusChanged)
  Q_PROPERTY(bool running READ running NOTIFY statusChanged)
  Q_PROPERTY(QString activeDevice READ activeDevice NOTIFY statusChanged)
  Q_PROPERTY(QString lastError READ lastError NOTIFY statusChanged)
  Q_PROPERTY(QString lastPhotoPath READ lastPhotoPath NOTIFY statusChanged)

 public:
  explicit CameraControlModel(std::string address, QObject* parent = nullptr);
  ~CameraControlModel() override;

  CameraControlModel(const CameraControlModel&) = delete;
  CameraControlModel& operator=(const CameraControlModel&) = delete;

  const QStringList& devices() const {
    return devices_;
  }
  bool busy() const {
    return busy_;
  }
  bool running() const {
    return running_;
  }
  const QString& activeDevice() const {
    return active_device_;
  }
  const QString& lastError() const {
    return last_error_;
  }
  const QString& lastPhotoPath() const {
    return last_photo_path_;
  }

  void Start();
  void Stop();

  Q_INVOKABLE void refreshDevices();
  Q_INVOKABLE void startPreview(const QString& device, int width = 640, int height = 480,
                                int fps = 30);
  Q_INVOKABLE void stopPreview();
  Q_INVOKABLE void takePhoto();

 signals:
  void devicesChanged();
  void statusChanged();

 private:
  enum class CommandType {
    kRefreshDevices,
    kStartPreview,
    kStopPreview,
    kTakePhoto,
  };

  struct Command {
    CommandType type = CommandType::kRefreshDevices;
    std::string device;
    std::uint32_t width = 640;
    std::uint32_t height = 480;
    std::uint32_t fps = 30;
  };

  void Enqueue(Command command);
  void Run();
  void PostDevices(QStringList devices, QString error);
  void PostStatus(bool running, QString active_device, QString error);
  void PostPhoto(QString path, QString error);

  std::string address_;
  std::atomic_bool worker_running_{false};
  std::mutex mutex_;
  std::condition_variable condition_;
  std::deque<Command> commands_;
  std::thread worker_;

  QStringList devices_;
  bool busy_ = false;
  bool running_ = false;
  QString active_device_;
  QString last_error_;
  QString last_photo_path_;
};

}  // namespace ui
}  // namespace cockpit
