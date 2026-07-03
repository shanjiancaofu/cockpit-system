#include "apps/cockpit-ui/camera/camera_frame_model.h"

#include <QImage>
#include <QSignalSpy>
#include <QtTest>

#include "apps/cockpit-ui/camera/camera_image_provider.h"

class CameraFrameModelTest final : public QObject {
  Q_OBJECT

 private slots:
  void StartsDisconnectedAndStale() {
    cockpit::ui::CameraImageProvider provider;
    cockpit::ui::CameraFrameModel model(&provider, nullptr, 50);
    QVERIFY(!model.connected());
    QVERIFY(!model.hasFrame());
    QVERIFY(!model.fresh());
  }

  void BecomesFreshThenStale() {
    cockpit::ui::CameraImageProvider provider;
    cockpit::ui::CameraFrameModel model(&provider, nullptr, 50);
    QSignalSpy freshness_spy(&model, &cockpit::ui::CameraFrameModel::freshnessChanged);

    model.SetConnected(true);
    model.UpdateFrame(QImage(4, 3, QImage::Format_RGB32), 7, 9);
    QVERIFY(model.connected());
    QVERIFY(model.hasFrame());
    QVERIFY(model.fresh());
    QCOMPARE(model.sequence(), quint64{7});
    QCOMPARE(freshness_spy.count(), 1);

    QTRY_VERIFY_WITH_TIMEOUT(!model.fresh(), 250);
    QCOMPARE(freshness_spy.count(), 2);
  }

  void DisconnectMarksFrameStale() {
    cockpit::ui::CameraImageProvider provider;
    cockpit::ui::CameraFrameModel model(&provider, nullptr, 1000);
    model.SetConnected(true);
    model.UpdateFrame(QImage(4, 3, QImage::Format_RGB32), 1, 1);
    QVERIFY(model.fresh());

    model.SetConnected(false);
    QVERIFY(!model.connected());
    QVERIFY(!model.fresh());
    QVERIFY(model.hasFrame());
  }
};

QTEST_GUILESS_MAIN(CameraFrameModelTest)

#include "camera_frame_model_test.moc"
