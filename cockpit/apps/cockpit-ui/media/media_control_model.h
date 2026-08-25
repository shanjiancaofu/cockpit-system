#pragma once

#include <QObject>
#include <QString>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "cockpit/core/base/macros.h"

namespace cockpit {
namespace ui {

enum class MediaPlaybackState {
  kUnavailable,
  kStopped,
  kPlaying,
  kPaused,
  kError,
};

struct MediaBackendStatus {
  bool available = false;
  MediaPlaybackState state = MediaPlaybackState::kUnavailable;
  std::string track_id;
  std::string title;
  std::string artist;
  std::string message;
};

struct MediaBackendResult {
  bool ok = false;
  MediaPlaybackState state = MediaPlaybackState::kError;
  std::string track_id;
  std::string title;
  std::string artist;
  std::string message;
};

class MediaPlayerBackend {
 public:
  virtual ~MediaPlayerBackend() = default;

  // Calls must be bounded. Track IDs are fixed by MediaControlModel; paths, URLs, arguments, and
  // shell text never cross this API.
  virtual MediaBackendStatus Query() = 0;
  virtual MediaBackendResult Play(const std::string& track_id) = 0;
  virtual MediaBackendResult Pause() = 0;
  virtual MediaBackendResult Resume() = 0;
  virtual MediaBackendResult Stop() = 0;
  virtual MediaBackendResult Next() = 0;
};

class MediaControlModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool available READ available NOTIFY statusChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY statusChanged)
  Q_PROPERTY(bool canPlay READ canPlay NOTIFY statusChanged)
  Q_PROPERTY(bool canPause READ canPause NOTIFY statusChanged)
  Q_PROPERTY(bool canStop READ canStop NOTIFY statusChanged)
  Q_PROPERTY(QString state READ state NOTIFY statusChanged)
  Q_PROPERTY(QString stateLabel READ stateLabel NOTIFY statusChanged)
  Q_PROPERTY(QString currentTrackId READ currentTrackId NOTIFY statusChanged)
  Q_PROPERTY(QString title READ title NOTIFY statusChanged)
  Q_PROPERTY(QString artist READ artist NOTIFY statusChanged)
  Q_PROPERTY(QString lastError READ lastError NOTIFY statusChanged)

 public:
  explicit MediaControlModel(std::unique_ptr<MediaPlayerBackend> backend = nullptr,
                             QObject* parent = nullptr);
  ~MediaControlModel() override;

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(MediaControlModel);

  bool available() const {
    return available_;
  }
  bool busy() const {
    return busy_;
  }
  bool canPlay() const;
  bool canPause() const;
  bool canStop() const;
  const QString& state() const {
    return state_;
  }
  QString stateLabel() const;
  const QString& currentTrackId() const {
    return current_track_id_;
  }
  const QString& title() const {
    return title_;
  }
  const QString& artist() const {
    return artist_;
  }
  const QString& lastError() const {
    return last_error_;
  }

  void Start();
  void Stop();
  Q_INVOKABLE void playDefault();
  Q_INVOKABLE void togglePause();
  Q_INVOKABLE void stopPlayback();
  Q_INVOKABLE void next();

  // Returns whether the request was accepted by the current UI/media state. The actual backend
  // operation remains serialized on MediaControlModel's worker and is reported through status.
  bool requestPlayDefault();
  bool requestPause();
  bool requestResume();

 signals:
  void statusChanged();

 private:
  enum class CommandType {
    kPlay,
    kPause,
    kResume,
    kStop,
    kNext,
  };

  void Enqueue(CommandType command);
  void Run();
  void ApplyStatus(MediaBackendStatus status);
  void ApplyResult(MediaBackendResult result);
  void SetLastError(QString error);
  static QString StateName(MediaPlaybackState state);

  const std::unique_ptr<MediaPlayerBackend> backend_;
  std::atomic_bool running_{false};
  std::mutex command_mutex_;
  std::condition_variable command_condition_;
  CommandType pending_command_ = CommandType::kPlay;
  bool has_pending_command_ = false;
  std::thread worker_;

  bool available_ = false;
  bool busy_ = false;
  QString state_ = QStringLiteral("UNAVAILABLE");
  QString current_track_id_;
  QString title_;
  QString artist_;
  QString status_message_;
  QString last_error_;
};

}  // namespace ui
}  // namespace cockpit
