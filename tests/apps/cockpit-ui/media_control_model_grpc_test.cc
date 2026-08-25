#include <unistd.h>

#include <QtTest>
#include <filesystem>
#include <string>

#include "cockpit/apps/cockpit-ui/media/grpc_media_player_backend.h"
#include "cockpit/apps/cockpit-ui/media/media_control_model.h"
#include "cockpit/library/media/media_grpc_service.h"
#include "cockpit/library/media/media_service.h"
#include "cockpit/modules/media/media_player.h"

namespace cockpit::ui {

class MediaControlModelGrpcTest final : public QObject {
  Q_OBJECT

 private slots:
  void RunsMockServiceLifecycle() {
    const std::filesystem::path socket_path =
        "/tmp/cockpit-ui-media-" + std::to_string(getpid()) + ".sock";
    std::error_code filesystem_error;
    std::filesystem::remove(socket_path, filesystem_error);
    media::MediaService service(media::CreateMockMediaPlayer());
    media::MediaGrpcService grpc_service(service);
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

}  // namespace cockpit::ui

QTEST_GUILESS_MAIN(cockpit::ui::MediaControlModelGrpcTest)
#include "media_control_model_grpc_test.moc"
