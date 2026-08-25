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
namespace sentinel {
class SentinelStatus;
}
}  // namespace proto
namespace ui {

class SentinelStatusModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool connected READ connected NOTIFY statusChanged)
  Q_PROPERTY(bool armed READ armed NOTIFY statusChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY statusChanged)
  Q_PROPERTY(QString state READ state NOTIFY statusChanged)
  Q_PROPERTY(QString stateLabel READ stateLabel NOTIFY statusChanged)
  Q_PROPERTY(qulonglong acceptedEvents READ acceptedEvents NOTIFY statusChanged)
  Q_PROPERTY(qulonglong suppressedEvents READ suppressedEvents NOTIFY statusChanged)
  Q_PROPERTY(QString lastSnapshotPath READ lastSnapshotPath NOTIFY statusChanged)
  Q_PROPERTY(QString lastError READ lastError NOTIFY statusChanged)

 public:
  explicit SentinelStatusModel(std::string address, QObject* parent = nullptr);
  ~SentinelStatusModel() override;
  COCKPIT_DISALLOW_COPY_AND_ASSIGN(SentinelStatusModel);
  bool connected() const {
    return connected_;
  }
  bool armed() const {
    return armed_;
  }
  bool busy() const {
    return busy_;
  }
  const QString& state() const {
    return state_;
  }
  const QString& stateLabel() const {
    return state_label_;
  }
  qulonglong acceptedEvents() const {
    return accepted_events_;
  }
  qulonglong suppressedEvents() const {
    return suppressed_events_;
  }
  const QString& lastSnapshotPath() const {
    return last_snapshot_path_;
  }
  const QString& lastError() const {
    return last_error_;
  }
  void Start();
  void Stop();
  Q_INVOKABLE void setArmed(bool armed);

 signals:
  void statusChanged();

 private:
  enum class Command { kNone, kArm, kDisarm };
  void Run();
  void ApplyStatus(const proto::sentinel::SentinelStatus& status, QString rpc_error);

  const std::string address_;
  std::atomic_bool running_{false};
  std::atomic<Command> command_{Command::kNone};
  std::mutex wait_mutex_;
  std::condition_variable wait_condition_;
  std::thread worker_;
  bool connected_ = false;
  bool armed_ = false;
  bool busy_ = false;
  QString state_ = QStringLiteral("DISCONNECTED");
  QString state_label_ = QStringLiteral("服务未连接");
  qulonglong accepted_events_ = 0;
  qulonglong suppressed_events_ = 0;
  QString last_snapshot_path_;
  QString last_error_;
};

}  // namespace ui
}  // namespace cockpit
