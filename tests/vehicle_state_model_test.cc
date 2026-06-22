#include "vehicle_state_model.h"

#include <QSignalSpy>
#include <QtTest>

class VehicleStateModelTest final : public QObject {
  Q_OBJECT

 private slots:
  void StartsDisconnectedAndStale() {
    cockpit::ui::VehicleStateModel model(nullptr, 50);
    QVERIFY(!model.connected());
    QVERIFY(!model.fresh());
  }

  void BecomesFreshThenStale() {
    cockpit::ui::VehicleStateModel model(nullptr, 50);
    QSignalSpy freshness_spy(&model, &cockpit::ui::VehicleStateModel::freshnessChanged);

    model.SetConnected(true);
    model.Update(1000, 42.0, 3, 80, true, QStringLiteral("test"));
    QVERIFY(model.connected());
    QVERIFY(model.fresh());
    QCOMPARE(freshness_spy.count(), 1);

    QTRY_VERIFY_WITH_TIMEOUT(!model.fresh(), 250);
    QCOMPARE(freshness_spy.count(), 2);
  }

  void DisconnectMarksDataStale() {
    cockpit::ui::VehicleStateModel model(nullptr, 1000);
    model.SetConnected(true);
    model.Update(1000, 42.0, 3, 80, true, QStringLiteral("test"));
    QVERIFY(model.fresh());

    model.SetConnected(false);
    QVERIFY(!model.connected());
    QVERIFY(!model.fresh());
  }
};

QTEST_GUILESS_MAIN(VehicleStateModelTest)

#include "vehicle_state_model_test.moc"
