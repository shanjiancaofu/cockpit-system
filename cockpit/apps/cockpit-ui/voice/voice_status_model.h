#pragma once

#include <QObject>
#include <QString>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "cockpit/core/base/macros.h"

namespace cockpit {
namespace proto {
namespace voice {
class VoiceInteractionStatus;
class InterruptVoiceResponse;
}  // namespace voice
}  // namespace proto

namespace ui {

class VoiceStatusModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool connected READ connected NOTIFY statusChanged)
  Q_PROPERTY(bool active READ active NOTIFY statusChanged)
  Q_PROPERTY(bool canInterrupt READ canInterrupt NOTIFY statusChanged)
  Q_PROPERTY(bool interruptPending READ interruptPending NOTIFY statusChanged)
  Q_PROPERTY(bool playbackAvailable READ playbackAvailable NOTIFY statusChanged)
  Q_PROPERTY(QString state READ state NOTIFY statusChanged)
  Q_PROPERTY(QString stateLabel READ stateLabel NOTIFY statusChanged)
  Q_PROPERTY(QString stateReason READ stateReason NOTIFY statusChanged)
  Q_PROPERTY(QString transcriptText READ transcriptText NOTIFY statusChanged)
  Q_PROPERTY(QString responseText READ responseText NOTIFY statusChanged)
  Q_PROPERTY(QString actionText READ actionText NOTIFY statusChanged)
  Q_PROPERTY(QString lastError READ lastError NOTIFY statusChanged)

 public:
  explicit VoiceStatusModel(std::string address, QObject* parent = nullptr);
  ~VoiceStatusModel() override;

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(VoiceStatusModel);

  bool connected() const {
    return connected_;
  }
  bool active() const {
    return active_;
  }
  bool canInterrupt() const {
    return can_interrupt_;
  }
  bool interruptPending() const {
    return interrupt_pending_;
  }
  bool playbackAvailable() const {
    return playback_available_;
  }
  const QString& state() const {
    return state_;
  }
  const QString& stateLabel() const {
    return state_label_;
  }
  const QString& stateReason() const {
    return state_reason_;
  }
  const QString& transcriptText() const {
    return transcript_text_;
  }
  const QString& responseText() const {
    return response_text_;
  }
  const QString& actionText() const {
    return action_text_;
  }
  const QString& lastError() const {
    return last_error_;
  }

  void Start();
  void Stop();
  Q_INVOKABLE void interrupt();

 signals:
  void statusChanged();

 private:
  void Run();
  void ApplyStatus(const proto::voice::VoiceInteractionStatus& status);
  void ApplyDisconnected(QString error);
  void ApplyInterruptResult(const proto::voice::InterruptVoiceResponse& response, QString error);

  const std::string address_;
  std::atomic_bool running_{false};
  std::atomic_bool interrupt_requested_{false};
  std::mutex wait_mutex_;
  std::condition_variable wait_condition_;
  std::thread worker_;

  bool connected_ = false;
  bool active_ = false;
  bool can_interrupt_ = false;
  bool interrupt_pending_ = false;
  bool playback_available_ = false;
  QString state_ = QStringLiteral("DISCONNECTED");
  QString state_label_ = QStringLiteral("服务未连接");
  QString state_reason_ = QStringLiteral("正在等待语音服务");
  QString transcript_text_;
  QString response_text_;
  QString action_text_;
  QString last_error_;
};

}  // namespace ui
}  // namespace cockpit
