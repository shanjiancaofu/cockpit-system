#include "cockpit/apps/cockpit-ui/apps/app_launcher_model.h"

#include <QMetaObject>
#include <chrono>
#include <optional>
#include <utility>

namespace cockpit {
namespace ui {
namespace {

constexpr auto kStatusPollInterval = std::chrono::milliseconds(500);

class UnavailableAppLauncherBackend final : public AppLauncherBackend {
 public:
  AppLauncherBackendStatus Query(const std::string& app_id) override {
    if (app_id == "local_media") {
      return {false, false, "本地播放器后端未接入"};
    }
    if (app_id == "phone_projection") {
      return {false, false, "手机投屏和音频焦点后端未接入"};
    }
    return {false, false, "当前 Ubuntu 不提供 Android 应用运行环境"};
  }

  AppLauncherBackendResult Launch(const std::string&) override {
    return {false, false, "应用启动后端不可用"};
  }

  AppLauncherBackendResult Stop(const std::string&) override {
    return {false, false, "应用停止后端不可用"};
  }
};

}  // namespace

AppLauncherModel::AppLauncherModel(std::unique_ptr<AppLauncherBackend> backend, QObject* parent)
    : QAbstractListModel(parent),
      backend_(backend == nullptr ? std::make_unique<UnavailableAppLauncherBackend>()
                                  : std::move(backend)) {
  items_ = {
      {QStringLiteral("local_media"),
       QStringLiteral("本地音乐"),
       QStringLiteral("L"),
       QStringLiteral("受控的原生播放器后端"),
       QStringLiteral("UNAVAILABLE"),
       {},
       false,
       false,
       false},
      {QStringLiteral("phone_projection"),
       QStringLiteral("手机互联"),
       QStringLiteral("P"),
       QStringLiteral("投屏、窗口和音频焦点后端"),
       QStringLiteral("UNAVAILABLE"),
       {},
       false,
       false,
       false},
      {QStringLiteral("android_apps"),
       QStringLiteral("Android 应用"),
       QStringLiteral("A"),
       QStringLiteral("Android Automotive 或受控容器后端"),
       QStringLiteral("UNAVAILABLE"),
       {},
       false,
       false,
       false},
  };
}

AppLauncherModel::~AppLauncherModel() {
  Stop();
}

int AppLauncherModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(items_.size());
}

QVariant AppLauncherModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(items_.size())) {
    return {};
  }
  const Item& item = items_[static_cast<std::size_t>(index.row())];
  switch (role) {
    case AppIdRole:
      return item.app_id;
    case DisplayNameRole:
      return item.display_name;
    case MarkRole:
      return item.mark;
    case DescriptionRole:
      return item.description;
    case StateRole:
      return item.state;
    case StateLabelRole:
      return StateLabel(item.state);
    case MessageRole:
      return item.message;
    case AvailableRole:
      return item.available;
    case RunningRole:
      return item.running;
    case BusyRole:
      return item.busy;
    default:
      return {};
  }
}

QHash<int, QByteArray> AppLauncherModel::roleNames() const {
  return {
      {AppIdRole, "appId"},        {DisplayNameRole, "displayName"},
      {MarkRole, "appMark"},       {DescriptionRole, "description"},
      {StateRole, "appState"},     {StateLabelRole, "stateLabel"},
      {MessageRole, "appMessage"}, {AvailableRole, "appAvailable"},
      {RunningRole, "appRunning"}, {BusyRole, "appBusy"},
  };
}

void AppLauncherModel::Start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true)) {
    return;
  }
  worker_ = std::thread(&AppLauncherModel::Run, this);
}

void AppLauncherModel::Stop() {
  running_.store(false);
  pending_command_.store(-1);
  command_condition_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void AppLauncherModel::launch(const QString& app_id) {
  const int row = FindRow(app_id);
  if (row < 0) {
    SetLastError(QStringLiteral("应用不在允许列表中"));
    return;
  }
  Item& item = items_[static_cast<std::size_t>(row)];
  if (!running_.load()) {
    SetLastError(QStringLiteral("App Launcher 尚未启动"));
    return;
  }
  if (!item.available) {
    SetLastError(item.message.isEmpty() ? QStringLiteral("应用后端不可用") : item.message);
    return;
  }
  if (item.running || item.busy) {
    SetLastError(item.running ? QStringLiteral("应用已经在运行")
                              : QStringLiteral("应用操作正在进行"));
    return;
  }
  Enqueue(row, CommandType::kLaunch);
}

void AppLauncherModel::stop(const QString& app_id) {
  const int row = FindRow(app_id);
  if (row < 0) {
    SetLastError(QStringLiteral("应用不在允许列表中"));
    return;
  }
  Item& item = items_[static_cast<std::size_t>(row)];
  if (!running_.load()) {
    SetLastError(QStringLiteral("App Launcher 尚未启动"));
    return;
  }
  if (!item.running || item.busy) {
    SetLastError(item.busy ? QStringLiteral("应用操作正在进行") : QStringLiteral("应用当前未运行"));
    return;
  }
  Enqueue(row, CommandType::kStop);
}

int AppLauncherModel::FindRow(const QString& app_id) const {
  for (std::size_t row = 0; row < items_.size(); ++row) {
    if (items_[row].app_id == app_id) {
      return static_cast<int>(row);
    }
  }
  return -1;
}

void AppLauncherModel::Enqueue(int row, CommandType type) {
  Item& item = items_[static_cast<std::size_t>(row)];
  item.busy = true;
  item.state =
      type == CommandType::kLaunch ? QStringLiteral("STARTING") : QStringLiteral("STOPPING");
  last_error_.clear();
  emit dataChanged(index(row), index(row), {StateRole, StateLabelRole, BusyRole});
  emit statusChanged();
  const int encoded_command = row * 2 + (type == CommandType::kStop ? 1 : 0);
  pending_command_.store(encoded_command);
  command_condition_.notify_one();
}

void AppLauncherModel::Run() {
  auto next_poll = std::chrono::steady_clock::now();
  while (running_.load()) {
    int encoded_command = -1;
    {
      std::unique_lock<std::mutex> lock(wait_mutex_);
      command_condition_.wait_until(lock, next_poll, [this] {
        return !running_.load() || pending_command_.load() >= 0;
      });
      if (!running_.load()) {
        break;
      }
      encoded_command = pending_command_.exchange(-1);
    }

    if (encoded_command >= 0) {
      const int row = encoded_command / 2;
      const CommandType type = encoded_command % 2 == 0 ? CommandType::kLaunch : CommandType::kStop;
      const std::string& app_id = app_ids_[static_cast<std::size_t>(row)];
      AppLauncherBackendResult result =
          type == CommandType::kLaunch ? backend_->Launch(app_id) : backend_->Stop(app_id);
      QMetaObject::invokeMethod(
          this,
          [this, app_id, type, result = std::move(result)] {
            ApplyOperation(app_id, type, result);
          },
          Qt::QueuedConnection);
      next_poll = std::chrono::steady_clock::now() + kStatusPollInterval;
      continue;
    }

    std::vector<StatusUpdate> updates;
    updates.reserve(app_ids_.size());
    for (const std::string& app_id : app_ids_) {
      updates.push_back({app_id, backend_->Query(app_id)});
    }
    QMetaObject::invokeMethod(
        this,
        [this, updates = std::move(updates)]() mutable {
          ApplyStatuses(updates);
        },
        Qt::QueuedConnection);
    next_poll = std::chrono::steady_clock::now() + kStatusPollInterval;
  }

  for (const std::string& app_id : app_ids_) {
    const AppLauncherBackendStatus status = backend_->Query(app_id);
    if (status.running) {
      static_cast<void>(backend_->Stop(app_id));
    }
  }
}

void AppLauncherModel::ApplyStatuses(const std::vector<StatusUpdate>& updates) {
  for (auto& update : updates) {
    const int row = FindRow(QString::fromStdString(update.app_id));
    if (row < 0) {
      continue;
    }
    Item& item = items_[static_cast<std::size_t>(row)];
    if (item.busy) {
      continue;
    }
    item.available = update.status.available;
    item.running = update.status.running;
    item.message = QString::fromStdString(update.status.message);
    if (item.state != QStringLiteral("FAILED")) {
      item.state = !item.available
                       ? QStringLiteral("UNAVAILABLE")
                       : (item.running ? QStringLiteral("RUNNING") : QStringLiteral("AVAILABLE"));
    }
    emit dataChanged(index(row), index(row));
  }
  emit statusChanged();
}

void AppLauncherModel::ApplyOperation(const std::string& app_id, CommandType,
                                      const AppLauncherBackendResult& result) {
  const int row = FindRow(QString::fromStdString(app_id));
  if (row < 0) {
    return;
  }
  Item& item = items_[static_cast<std::size_t>(row)];
  item.busy = false;
  item.running = result.running;
  item.message = QString::fromStdString(result.message);
  if (result.ok) {
    item.available = true;
    item.state = item.running ? QStringLiteral("RUNNING") : QStringLiteral("AVAILABLE");
    last_error_.clear();
  } else {
    item.state = QStringLiteral("FAILED");
    last_error_ = item.message.isEmpty() ? QStringLiteral("应用操作失败") : item.message;
  }
  emit dataChanged(index(row), index(row));
  emit statusChanged();
}

void AppLauncherModel::SetLastError(QString error) {
  last_error_ = std::move(error);
  emit statusChanged();
}

QString AppLauncherModel::StateLabel(const QString& state) {
  if (state == QStringLiteral("AVAILABLE")) {
    return QStringLiteral("可启动");
  }
  if (state == QStringLiteral("STARTING")) {
    return QStringLiteral("启动中");
  }
  if (state == QStringLiteral("RUNNING")) {
    return QStringLiteral("运行中");
  }
  if (state == QStringLiteral("STOPPING")) {
    return QStringLiteral("停止中");
  }
  if (state == QStringLiteral("FAILED")) {
    return QStringLiteral("失败");
  }
  return QStringLiteral("不可用");
}

}  // namespace ui
}  // namespace cockpit
