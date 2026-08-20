#include "cockpit/apps/cockpit-ui/media/media_control_model.h"

#include <unistd.h>

#include <QtTest>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#include "cockpit/apps/cockpit-ui/media/grpc_media_player_backend.h"
#include "cockpit/library/media/media_grpc_service.h"
#include "cockpit/library/media/media_service.h"
#include "cockpit/modules/media/media_player.h"

namespace cockpit {
namespace ui {
namespace {

class FakeMediaPlayerBackend final : public MediaPlayerBackend {
 public:
  MediaBackendStatus Query() override {
    return {true, state_, track_id_, title_, "Fixture Artist", "fixture backend ready"};
  }

  MediaBackendResult Play(const std::string& track_id) override {
    ++play_count;
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    if (fail_play.exchange(false)) {
      return {false, MediaPlaybackState::kError, {}, {}, {}, "fixture play failed"};
    }
    if (track_id != "default_track") {
      return {false, MediaPlaybackState::kError, {}, {}, {}, "track id rejected"};
    }
    state_ = MediaPlaybackState::kPlaying;
    track_id_ = track_id;
    title_ = "Fixture Track 1";
    return CurrentResult("fixture playing");
  }

  MediaBackendResult Pause() override {
    ++pause_count;
    if (state_ != MediaPlaybackState::kPlaying) {
      return {false,  MediaPlaybackState::kError, track_id_,
              title_, "Fixture Artist",           "fixture is not playing"};
    }
    state_ = MediaPlaybackState::kPaused;
    return CurrentResult("fixture paused");
  }

  MediaBackendResult Resume() override {
    ++resume_count;
    if (state_ != MediaPlaybackState::kPaused) {
      return {false,  MediaPlaybackState::kError, track_id_,
              title_, "Fixture Artist",           "fixture is not paused"};
    }
    state_ = MediaPlaybackState::kPlaying;
    return CurrentResult("fixture resumed");
  }

  MediaBackendResult Stop() override {
    ++stop_count;
    state_ = MediaPlaybackState::kStopped;
    return CurrentResult("fixture stopped");
  }

  MediaBackendResult Next() override {
    ++next_count;
    if (state_ != MediaPlaybackState::kPlaying && state_ != MediaPlaybackState::kPaused) {
      return {false,  MediaPlaybackState::kError, track_id_,
              title_, "Fixture Artist",           "fixture cannot change track"};
    }
    track_id_ = "next_track";
    title_ = "Fixture Track 2";
    return CurrentResult("fixture changed track");
  }

  MediaBackendResult CurrentResult(std::string message) const {
    return {true, state_, track_id_, title_, "Fixture Artist", std::move(message)};
  }

  std::atomic<int> play_count{0};
  std::atomic<int> pause_count{0};
  std::atomic<int> resume_count{0};
  std::atomic<int> stop_count{0};
  std::atomic<int> next_count{0};
  std::atomic_bool fail_play{false};

 private:
  MediaPlaybackState state_ = MediaPlaybackState::kStopped;
  std::string track_id_;
  std::string title_;
};

}  // namespace

class MediaControlModelTest final : public QObject {
  Q_OBJECT

 private slots:
  void RunsBoundedFixtureLifecycle() {
    auto backend = std::make_unique<FakeMediaPlayerBackend>();
    FakeMediaPlayerBackend* backend_observer = backend.get();
    MediaControlModel model(std::move(backend));
    model.Start();
    QTRY_VERIFY_WITH_TIMEOUT(model.available(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(model.state(), QStringLiteral("STOPPED"), 2000);
    QVERIFY(model.canPlay());

    model.playDefault();
    QVERIFY(model.busy());
    model.playDefault();
    QTRY_COMPARE_WITH_TIMEOUT(backend_observer->play_count.load(), 1, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(model.state(), QStringLiteral("PLAYING"), 2000);
    QCOMPARE(model.title(), QStringLiteral("Fixture Track 1"));

    model.togglePause();
    QTRY_COMPARE_WITH_TIMEOUT(model.state(), QStringLiteral("PAUSED"), 2000);
    QCOMPARE(backend_observer->pause_count.load(), 1);
    model.togglePause();
    QTRY_COMPARE_WITH_TIMEOUT(model.state(), QStringLiteral("PLAYING"), 2000);
    QCOMPARE(backend_observer->resume_count.load(), 1);

    model.next();
    QTRY_COMPARE_WITH_TIMEOUT(model.currentTrackId(), QStringLiteral("next_track"), 2000);
    QCOMPARE(model.title(), QStringLiteral("Fixture Track 2"));
    QCOMPARE(backend_observer->next_count.load(), 1);

    model.stopPlayback();
    QTRY_COMPARE_WITH_TIMEOUT(model.state(), QStringLiteral("STOPPED"), 2000);
    QCOMPARE(backend_observer->stop_count.load(), 1);

    backend_observer->fail_play.store(true);
    model.playDefault();
    QTRY_COMPARE_WITH_TIMEOUT(model.state(), QStringLiteral("ERROR"), 2000);
    QCOMPARE(model.lastError(), QStringLiteral("fixture play failed"));
    model.playDefault();
    QTRY_COMPARE_WITH_TIMEOUT(model.state(), QStringLiteral("PLAYING"), 2000);
    QCOMPARE(backend_observer->play_count.load(), 3);

    model.Stop();
    QCOMPARE(backend_observer->stop_count.load(), 2);
  }

  void DefaultBackendRemainsUnavailable() {
    MediaControlModel model;
    model.Start();
    QTRY_COMPARE_WITH_TIMEOUT(model.state(), QStringLiteral("UNAVAILABLE"), 2000);
    QVERIFY(!model.available());
    model.playDefault();
    QVERIFY(!model.lastError().isEmpty());
    model.Stop();
  }

  void GrpcBackendRunsMockServiceLifecycle() {
    const std::filesystem::path socket_path =
        "/tmp/cockpit-ui-media-" + std::to_string(getpid()) + ".sock";
    std::error_code filesystem_error;
    std::filesystem::remove(socket_path, filesystem_error);
    cockpit::media::MediaService service(cockpit::media::CreateMockMediaPlayer());
    cockpit::media::MediaGrpcService grpc_service(service);
    const std::string address = "unix:" + socket_path.string();
    QVERIFY(grpc_service.Start(address));

    MediaControlModel model(CreateGrpcMediaPlayerBackend(address));
    model.Start();
    QTRY_VERIFY_WITH_TIMEOUT(model.available(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(model.state(), QStringLiteral("STOPPED"), 2000);

    model.playDefault();
    QTRY_COMPARE_WITH_TIMEOUT(model.state(), QStringLiteral("PLAYING"), 2000);
    QCOMPARE(model.currentTrackId(), QStringLiteral("mock_track_one"));
    QCOMPARE(model.title(), QStringLiteral("Mock Track One"));
    model.togglePause();
    QTRY_COMPARE_WITH_TIMEOUT(model.state(), QStringLiteral("PAUSED"), 2000);
    model.togglePause();
    QTRY_COMPARE_WITH_TIMEOUT(model.state(), QStringLiteral("PLAYING"), 2000);
    model.stopPlayback();
    QTRY_COMPARE_WITH_TIMEOUT(model.state(), QStringLiteral("STOPPED"), 2000);

    model.Stop();
    grpc_service.Shutdown();
    std::filesystem::remove(socket_path, filesystem_error);
  }
};

}  // namespace ui
}  // namespace cockpit

QTEST_GUILESS_MAIN(cockpit::ui::MediaControlModelTest)

#include "media_control_model_test.moc"
