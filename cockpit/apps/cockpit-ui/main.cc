#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include <QUrl>
#include <utility>
#include <vector>

#include "cockpit/apps/cockpit-ui/camera/camera_control_model.h"
#include "cockpit/apps/cockpit-ui/camera/camera_frame_client.h"
#include "cockpit/apps/cockpit-ui/camera/camera_frame_model.h"
#include "cockpit/apps/cockpit-ui/camera/camera_image_provider.h"
#include "cockpit/apps/cockpit-ui/health/service_health_model.h"
#include "cockpit/apps/cockpit-ui/vehicle/gateway_client.h"
#include "cockpit/apps/cockpit-ui/vehicle/vehicle_state_model.h"
#include "cockpit/core/runtime/ServiceRuntime.h"

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QGuiApplication::setApplicationName(QStringLiteral("Smart Cockpit System"));

  auto runtime = cockpit::runtime::ServiceRuntime::Create(argc, argv, "cockpit-ui");
  cockpit::ui::VehicleStateModel vehicle_state;
  cockpit::ui::GatewayClient gateway_client(runtime.config().services().gateway.grpc.listen_address,
                                            &vehicle_state);

  auto* camera_image_provider = new cockpit::ui::CameraImageProvider();
  cockpit::ui::CameraFrameModel camera_frame(camera_image_provider);
  cockpit::ui::CameraFrameClient camera_client(
      runtime.config().services().camera.shared_memory_name, &camera_frame);
  cockpit::ui::CameraControlModel camera_control(
      runtime.config().services().camera.grpc.listen_address);
  std::vector<cockpit::ui::ServiceHealthEndpoint> health_endpoints = {
      {QStringLiteral("Gateway"), "cockpit-gateway-service",
       runtime.config().services().gateway.grpc.listen_address},
      {QStringLiteral("Audio"), "audio-service",
       runtime.config().services().audio.grpc.listen_address},
      {QStringLiteral("Voice"), "voice-interaction-service",
       runtime.config().services().voice_interaction.grpc.listen_address},
      {QStringLiteral("Camera"), "camera-service",
       runtime.config().services().camera.grpc.listen_address},
      {QStringLiteral("Recording"), "recording-service",
       runtime.config().services().recording.grpc.listen_address},
  };
  cockpit::ui::ServiceHealthModel service_health(std::move(health_endpoints));

  QQmlApplicationEngine engine;
  engine.addImageProvider(QStringLiteral("camera"), camera_image_provider);
  engine.rootContext()->setContextProperty(QStringLiteral("vehicleState"), &vehicle_state);
  engine.rootContext()->setContextProperty(QStringLiteral("cameraFrame"), &camera_frame);
  engine.rootContext()->setContextProperty(QStringLiteral("cameraControl"), &camera_control);
  engine.rootContext()->setContextProperty(QStringLiteral("serviceHealth"), &service_health);
  engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
  if (engine.rootObjects().isEmpty()) {
    runtime.MarkStopped();
    return 1;
  }

  QObject::connect(&app, &QCoreApplication::aboutToQuit,
                   [&gateway_client, &camera_client, &camera_control, &service_health] {
                     service_health.Stop();
                     camera_control.Stop();
                     camera_client.Stop();
                     gateway_client.Stop();
                   });
  QTimer shutdown_timer;
  shutdown_timer.setInterval(100);
  QObject::connect(&shutdown_timer, &QTimer::timeout, [&app, &runtime] {
    if (runtime.ShouldStop()) {
      app.quit();
    }
  });
  shutdown_timer.start();

  gateway_client.Start();
  camera_client.Start();
  camera_control.Start();
  service_health.Start();
  const int result = app.exec();
  runtime.MarkStopped();
  return result;
}
