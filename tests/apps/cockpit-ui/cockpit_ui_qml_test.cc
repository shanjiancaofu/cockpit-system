#include <QEventLoop>
#include <QGuiApplication>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickWindow>
#include <QTimer>
#include <QVariant>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

#include "cockpit/apps/cockpit-ui/camera/camera_control_model.h"
#include "cockpit/apps/cockpit-ui/camera/camera_frame_model.h"
#include "cockpit/apps/cockpit-ui/camera/camera_image_provider.h"
#include "cockpit/apps/cockpit-ui/health/service_health_model.h"
#include "cockpit/apps/cockpit-ui/hmi_control.h"
#include "cockpit/apps/cockpit-ui/vehicle/vehicle_state_model.h"

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);

  cockpit::ui::VehicleStateModel vehicle_state;
  vehicle_state.SetConnected(true);
  vehicle_state.Update(1, 42.0, 3, 78, false, QStringLiteral("qml-test"));
  auto* camera_image_provider = new cockpit::ui::CameraImageProvider();
  cockpit::ui::CameraFrameModel camera_frame(camera_image_provider);
  cockpit::ui::CameraControlModel camera_control("unix:/tmp/cockpit-ui-qml-test-camera.sock");
  cockpit::ui::ServiceHealthModel service_health(std::vector<cockpit::ui::ServiceHealthEndpoint>{});
  cockpit::ui::HmiControl hmi_control;

  QQmlApplicationEngine engine;
  bool qml_warning = false;
  QObject::connect(&engine, &QQmlEngine::warnings,
                   [&qml_warning](const QList<QQmlError>& warnings) {
                     qml_warning = qml_warning || !warnings.empty();
                   });
  engine.addImageProvider(QStringLiteral("camera"), camera_image_provider);
  engine.rootContext()->setContextProperty(QStringLiteral("vehicleState"), &vehicle_state);
  engine.rootContext()->setContextProperty(QStringLiteral("cameraFrame"), &camera_frame);
  engine.rootContext()->setContextProperty(QStringLiteral("cameraControl"), &camera_control);
  engine.rootContext()->setContextProperty(QStringLiteral("serviceHealth"), &service_health);
  engine.rootContext()->setContextProperty(QStringLiteral("hmiControl"), &hmi_control);
  engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
  QCoreApplication::processEvents();
  if (qml_warning || engine.rootObjects().size() != 1) {
    std::cerr << "cockpit UI QML root did not load\n";
    return 1;
  }

  QObject* root = engine.rootObjects().front();
  QObject* shell = root->findChild<QObject*>(QStringLiteral("cockpitShell"));
  QObject* page_stack = root->findChild<QObject*>(QStringLiteral("pageStack"));
  QObject* app_dock = root->findChild<QObject*>(QStringLiteral("appDock"));
  if (shell == nullptr || page_stack == nullptr || app_dock == nullptr ||
      root->property("width").toInt() != 1280 || root->property("height").toInt() != 720) {
    std::cerr << "cockpit UI QML structure mismatch\n";
    return 1;
  }

  const char* screenshot_path = std::getenv("COCKPIT_UI_QML_SCREENSHOT");
  if (screenshot_path != nullptr && screenshot_path[0] != '\0') {
    const char* screenshot_view = std::getenv("COCKPIT_UI_QML_SCREENSHOT_VIEW");
    if (screenshot_view != nullptr && screenshot_view[0] != '\0') {
      hmi_control.setCurrentView(std::atoi(screenshot_view));
      QCoreApplication::processEvents();
    }
    auto* window = qobject_cast<QQuickWindow*>(root);
    if (window == nullptr) {
      std::cerr << "cockpit UI root is not a QQuickWindow\n";
      return 1;
    }
    window->show();
    QEventLoop render_wait;
    QTimer::singleShot(150, &render_wait, &QEventLoop::quit);
    render_wait.exec();
    if (!window->grabWindow().save(QString::fromLocal8Bit(screenshot_path))) {
      std::cerr << "cockpit UI screenshot failed\n";
      return 1;
    }
  }

  hmi_control.setCurrentView(cockpit::ui::HmiControl::kSettingsView);
  QCoreApplication::processEvents();
  if (page_stack->property("currentIndex").toInt() != cockpit::ui::HmiControl::kSettingsView ||
      app_dock->property("currentIndex").toInt() != cockpit::ui::HmiControl::kSettingsView) {
    std::cerr << "cockpit UI navigation binding mismatch\n";
    return 1;
  }
  return 0;
}
