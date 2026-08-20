#include "cockpit/apps/cockpit-ui/media/media_control_model.h"

#include <QMetaObject>
#include <chrono>
#include <optional>
#include <utility>

namespace cockpit {
namespace ui {
namespace {

constexpr auto kStatusPollInterval = std::chrono::milliseconds(250);
constexpr char kDefaultTrackId[] = "default_track";

class UnavailableMediaPlayerBackend final : public MediaPlayerBackend {
 public:
  MediaBackendStatus Query() override {
    return {false, MediaPlaybackState::kUnavailable,   {}, {},
            {},    "本地媒体 owner 和音频焦点尚未接入"};
  }

  MediaBackendResult Play(const std::string&) override {
    return {false, MediaPlaybackState::kError, {}, {}, {}, "本地媒体后端不可用"};
  }
  MediaBackendResult Pause() override {
    return {false, MediaPlaybackState::kError, {}, {}, {}, "本地媒体后端不可用"};
  }
  MediaBackendResult Resume() override {
    return {false, MediaPlaybackState::kError, {}, {}, {}, "本地媒体后端不可用"};
  }
  MediaBackendResult Stop() override {
    return {false, MediaPlaybackState::kError, {}, {}, {}, "本地媒体后端不可用"};
  }
  MediaBackendResult Next() override {
    return {false, MediaPlaybackState::kError, {}, {}, {}, "本地媒体后端不可用"};
  }
};

}  // namespace

MediaControlModel::MediaControlModel(std::unique_ptr<MediaPlayerBackend> backend, QObject* parent)
    : QObject(parent),
      backend_(backend == nullptr ? std::make_unique<UnavailableMediaPlayerBackend>()
                                  : std::move(backend)) {
}

MediaControlModel::~MediaControlModel() {
  Stop();
}

bool MediaControlModel::canPlay() const {
  return available_ && !busy_ &&
         (state_ == QStringLiteral("STOPPED") || state_ == QStringLiteral("ERROR"));
}

bool MediaControlModel::canPause() const {
  return available_ && !busy_ &&
         (state_ == QStringLiteral("PLAYING") || state_ == QStringLiteral("PAUSED"));
}

bool MediaControlModel::canStop() const {
  return available_ && !busy_ &&
         (state_ == QStringLiteral("PLAYING") || state_ == QStringLiteral("PAUSED"));
}

QString MediaControlModel::stateLabel() const {
  if (busy_) {
    return QStringLiteral("处理中");
  }
  if (state_ == QStringLiteral("STOPPED")) {
    return QStringLiteral("已停止");
  }
  if (state_ == QStringLiteral("PLAYING")) {
    return QStringLiteral("播放中");
  }
  if (state_ == QStringLiteral("PAUSED")) {
    return QStringLiteral("已暂停");
  }
  if (state_ == QStringLiteral("ERROR")) {
    return QStringLiteral("播放失败");
  }
  return QStringLiteral("不可用");
}

void MediaControlModel::Start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true)) {
    return;
  }
  worker_ = std::thread(&MediaControlModel::Run, this);
}

void MediaControlModel::Stop() {
  running_.store(false);
  command_condition_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void MediaControlModel::playDefault() {
  if (!running_.load()) {
    SetLastError(QStringLiteral("媒体控制尚未启动"));
    return;
  }
  if (!canPlay()) {
    SetLastError(!available_ ? (status_message_.isEmpty() ? QStringLiteral("媒体后端不可用")
                                                          : status_message_)
                             : QStringLiteral("当前状态不能开始播放"));
    return;
  }
  Enqueue(CommandType::kPlay);
}

void MediaControlModel::togglePause() {
  if (!running_.load() || !canPause()) {
    SetLastError(QStringLiteral("当前状态不能暂停或继续"));
    return;
  }
  Enqueue(state_ == QStringLiteral("PLAYING") ? CommandType::kPause : CommandType::kResume);
}

void MediaControlModel::stopPlayback() {
  if (!running_.load() || !canStop()) {
    SetLastError(QStringLiteral("当前没有可停止的媒体播放"));
    return;
  }
  Enqueue(CommandType::kStop);
}

void MediaControlModel::next() {
  if (!running_.load() || !canPause()) {
    SetLastError(QStringLiteral("当前状态不能切换曲目"));
    return;
  }
  Enqueue(CommandType::kNext);
}

void MediaControlModel::Enqueue(CommandType command) {
  busy_ = true;
  last_error_.clear();
  emit statusChanged();
  {
    std::lock_guard<std::mutex> lock(command_mutex_);
    commands_.push_back(command);
  }
  command_condition_.notify_one();
}

void MediaControlModel::Run() {
  auto next_poll = std::chrono::steady_clock::now();
  while (running_.load()) {
    std::optional<CommandType> command;
    {
      std::unique_lock<std::mutex> lock(command_mutex_);
      command_condition_.wait_until(lock, next_poll, [this] {
        return !running_.load() || !commands_.empty();
      });
      if (!running_.load()) {
        break;
      }
      if (!commands_.empty()) {
        command = commands_.front();
        commands_.pop_front();
      }
    }

    if (command.has_value()) {
      MediaBackendResult result;
      switch (*command) {
        case CommandType::kPlay:
          result = backend_->Play(kDefaultTrackId);
          break;
        case CommandType::kPause:
          result = backend_->Pause();
          break;
        case CommandType::kResume:
          result = backend_->Resume();
          break;
        case CommandType::kStop:
          result = backend_->Stop();
          break;
        case CommandType::kNext:
          result = backend_->Next();
          break;
      }
      QMetaObject::invokeMethod(
          this,
          [this, result = std::move(result)]() mutable {
            ApplyResult(std::move(result));
          },
          Qt::QueuedConnection);
      next_poll = std::chrono::steady_clock::now() + kStatusPollInterval;
      continue;
    }

    MediaBackendStatus status = backend_->Query();
    QMetaObject::invokeMethod(
        this,
        [this, status = std::move(status)]() mutable {
          ApplyStatus(std::move(status));
        },
        Qt::QueuedConnection);
    next_poll = std::chrono::steady_clock::now() + kStatusPollInterval;
  }

  const MediaBackendStatus status = backend_->Query();
  if (status.state == MediaPlaybackState::kPlaying || status.state == MediaPlaybackState::kPaused) {
    static_cast<void>(backend_->Stop());
  }
}

void MediaControlModel::ApplyStatus(MediaBackendStatus status) {
  if (busy_) {
    return;
  }
  available_ = status.available;
  status_message_ = QString::fromStdString(status.message);
  if (state_ != QStringLiteral("ERROR")) {
    state_ = StateName(status.state);
    current_track_id_ = QString::fromStdString(status.track_id);
    title_ = QString::fromStdString(status.title);
    artist_ = QString::fromStdString(status.artist);
  }
  emit statusChanged();
}

void MediaControlModel::ApplyResult(MediaBackendResult result) {
  busy_ = false;
  status_message_ = QString::fromStdString(result.message);
  current_track_id_ = QString::fromStdString(result.track_id);
  title_ = QString::fromStdString(result.title);
  artist_ = QString::fromStdString(result.artist);
  if (result.ok) {
    available_ = true;
    state_ = StateName(result.state);
    last_error_.clear();
  } else {
    state_ = QStringLiteral("ERROR");
    last_error_ = status_message_.isEmpty() ? QStringLiteral("媒体操作失败") : status_message_;
  }
  emit statusChanged();
}

void MediaControlModel::SetLastError(QString error) {
  last_error_ = std::move(error);
  emit statusChanged();
}

QString MediaControlModel::StateName(MediaPlaybackState state) {
  switch (state) {
    case MediaPlaybackState::kStopped:
      return QStringLiteral("STOPPED");
    case MediaPlaybackState::kPlaying:
      return QStringLiteral("PLAYING");
    case MediaPlaybackState::kPaused:
      return QStringLiteral("PAUSED");
    case MediaPlaybackState::kError:
      return QStringLiteral("ERROR");
    case MediaPlaybackState::kUnavailable:
      return QStringLiteral("UNAVAILABLE");
  }
  return QStringLiteral("UNAVAILABLE");
}

}  // namespace ui
}  // namespace cockpit
