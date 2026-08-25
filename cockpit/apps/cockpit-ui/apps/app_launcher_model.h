#pragma once

#include <QAbstractListModel>
#include <QString>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "cockpit/core/base/macros.h"

namespace cockpit {
namespace ui {

struct AppLauncherBackendStatus {
  bool available = false;
  bool running = false;
  std::string message;
};

struct AppLauncherBackendResult {
  bool ok = false;
  bool running = false;
  std::string message;
};

class AppLauncherBackend {
 public:
  virtual ~AppLauncherBackend() = default;

  // The model only passes allowlisted IDs. Implementations must keep each call bounded and map
  // IDs to fixed launch contracts internally; paths, arguments, and shell text never cross this
  // API.
  virtual AppLauncherBackendStatus Query(const std::string& app_id) = 0;
  virtual AppLauncherBackendResult Launch(const std::string& app_id) = 0;
  virtual AppLauncherBackendResult Stop(const std::string& app_id) = 0;
};

class AppLauncherModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(QString lastError READ lastError NOTIFY statusChanged)

 public:
  enum Role {
    AppIdRole = Qt::UserRole + 1,
    DisplayNameRole,
    MarkRole,
    DescriptionRole,
    StateRole,
    StateLabelRole,
    MessageRole,
    AvailableRole,
    RunningRole,
    BusyRole,
  };

  explicit AppLauncherModel(std::unique_ptr<AppLauncherBackend> backend = nullptr,
                            QObject* parent = nullptr);
  ~AppLauncherModel() override;

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(AppLauncherModel);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  const QString& lastError() const {
    return last_error_;
  }

  void Start();
  void Stop();
  Q_INVOKABLE void launch(const QString& app_id);
  Q_INVOKABLE void stop(const QString& app_id);

 signals:
  void statusChanged();

 private:
  enum class CommandType {
    kLaunch,
    kStop,
  };

  struct Item {
    QString app_id;
    QString display_name;
    QString mark;
    QString description;
    QString state = QStringLiteral("UNAVAILABLE");
    QString message;
    bool available = false;
    bool running = false;
    bool busy = false;
  };

  struct Command {
    CommandType type = CommandType::kLaunch;
    std::string app_id;
  };

  struct StatusUpdate {
    std::string app_id;
    AppLauncherBackendStatus status;
  };

  int FindRow(const QString& app_id) const;
  void Enqueue(int row, CommandType type);
  void Run();
  void ApplyStatuses(std::vector<StatusUpdate> updates);
  void ApplyOperation(const std::string& app_id, CommandType type, AppLauncherBackendResult result);
  void SetLastError(QString error);
  static QString StateLabel(const QString& state);

  std::vector<Item> items_;
  const std::vector<std::string> app_ids_{"local_media", "phone_projection", "android_apps"};
  const std::unique_ptr<AppLauncherBackend> backend_;
  std::atomic_bool running_{false};
  std::mutex command_mutex_;
  std::condition_variable command_condition_;
  Command pending_command_;
  bool has_pending_command_ = false;
  std::thread worker_;
  QString last_error_;
};

}  // namespace ui
}  // namespace cockpit
