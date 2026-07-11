#include "cockpit/apps/cockpit-ui/health/service_health_model.h"

#include <QVariantMap>
#include <QtTest>

namespace cockpit {
namespace ui {

class ServiceHealthModelTest final : public QObject {
  Q_OBJECT

 private slots:
  void RecordsProblemsRecoveryAndBoundedHistory() {
    ServiceHealthModel model(
        {{QStringLiteral("Gateway"), "cockpit-gateway-service", "127.0.0.1:50051"}});
    const QModelIndex index = model.index(0);
    qint64 checked_at_ms = 1000;

    model.PostSamples({{QStringLiteral("OK"), QStringLiteral("online"), {}, ++checked_at_ms}});
    QTRY_COMPARE(model.data(index, ServiceHealthModel::StateRole).toString(), QStringLiteral("OK"));
    QCOMPARE(model.recentTransitions().size(), 0);

    model.PostSamples({{QStringLiteral("DEGRADED"), QStringLiteral("input delayed"),
                        QStringLiteral("vehicle stream stale"), ++checked_at_ms}});
    QTRY_COMPARE(model.data(index, ServiceHealthModel::StateRole).toString(),
                 QStringLiteral("DEGRADED"));
    QCOMPARE(model.data(index, ServiceHealthModel::LastProblemStateRole).toString(),
             QStringLiteral("DEGRADED"));
    QCOMPARE(model.data(index, ServiceHealthModel::LastProblemReasonRole).toString(),
             QStringLiteral("vehicle stream stale"));

    model.PostSamples({{QStringLiteral("OK"), QStringLiteral("recovered"), {}, ++checked_at_ms}});
    QTRY_COMPARE(model.data(index, ServiceHealthModel::StateRole).toString(), QStringLiteral("OK"));
    QCOMPARE(model.data(index, ServiceHealthModel::LastProblemStateRole).toString(),
             QStringLiteral("DEGRADED"));
    QCOMPARE(model.recentTransitions().front().toMap().value("toState").toString(),
             QStringLiteral("OK"));

    for (int transition = 0; transition < 34; ++transition) {
      const bool degraded = transition % 2 == 0;
      const QString state = degraded ? QStringLiteral("DEGRADED") : QStringLiteral("OK");
      model.PostSamples({{state,
                          degraded ? QStringLiteral("degraded") : QStringLiteral("recovered"),
                          {},
                          ++checked_at_ms}});
      QTRY_COMPARE(model.data(index, ServiceHealthModel::StateRole).toString(), state);
    }
    QCOMPARE(model.recentTransitions().size(), 32);

    ServiceHealthModel initially_faulted(
        {{QStringLiteral("Camera"), "camera-service", "127.0.0.1:50054"}});
    const QModelIndex faulted_index = initially_faulted.index(0);
    initially_faulted.PostSamples({{QStringLiteral("FAULTED"), QStringLiteral("preview failed"),
                                    QStringLiteral("camera disconnected"), 2000}});
    QTRY_COMPARE(
        initially_faulted.data(faulted_index, ServiceHealthModel::LastProblemStateRole).toString(),
        QStringLiteral("FAULTED"));
    QCOMPARE(initially_faulted.recentTransitions().size(), 0);
  }
};

}  // namespace ui
}  // namespace cockpit

QTEST_GUILESS_MAIN(cockpit::ui::ServiceHealthModelTest)

#include "service_health_model_test.moc"
