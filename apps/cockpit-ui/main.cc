#include "core/runtime/ServiceRuntime.h"
#include "gateway_client.h"
#include "vehicle_state_model.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include <QUrl>

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QGuiApplication::setApplicationName(QStringLiteral("Smart Cockpit System"));

  auto runtime =
      cockpit::runtime::ServiceRuntime::Create(argc, argv, "cockpit-ui");
  cockpit::ui::VehicleStateModel vehicle_state;
  cockpit::ui::GatewayClient gateway_client(
      runtime.config().services().gateway.grpc.listen_address, &vehicle_state);

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("vehicleState"),
                                           &vehicle_state);
  engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
  if (engine.rootObjects().isEmpty()) {
    runtime.MarkStopped();
    return 1;
  }

  QObject::connect(&app, &QCoreApplication::aboutToQuit,
                   [&gateway_client] { gateway_client.Stop(); });
  QTimer shutdown_timer;
  shutdown_timer.setInterval(100);
  QObject::connect(&shutdown_timer, &QTimer::timeout, [&app, &runtime] {
    if (runtime.ShouldStop()) {
      app.quit();
    }
  });
  shutdown_timer.start();

  gateway_client.Start();
  const int result = app.exec();
  runtime.MarkStopped();
  return result;
}
