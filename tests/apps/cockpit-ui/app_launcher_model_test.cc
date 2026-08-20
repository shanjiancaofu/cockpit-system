#include "cockpit/apps/cockpit-ui/apps/app_launcher_model.h"

#include <QtTest>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace cockpit {
namespace ui {
namespace {

class FakeAppLauncherBackend final : public AppLauncherBackend {
 public:
  AppLauncherBackendStatus Query(const std::string& app_id) override {
    if (app_id == "local_media") {
      return {true, local_media_running.load(), "本地播放器可用"};
    }
    if (app_id == "phone_projection") {
      return {true, phone_projection_running.load(), "手机互联可用"};
    }
    return {false, false, "Android 后端不可用"};
  }

  AppLauncherBackendResult Launch(const std::string& app_id) override {
    ++launch_count;
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    if (app_id == "phone_projection" && fail_phone_launch.load()) {
      return {false, false, "手机互联启动失败"};
    }
    if (app_id == "local_media") {
      local_media_running.store(true);
      return {true, true, "本地播放器运行中"};
    }
    if (app_id == "phone_projection") {
      phone_projection_running.store(true);
      return {true, true, "手机互联运行中"};
    }
    return {false, false, "未知应用"};
  }

  AppLauncherBackendResult Stop(const std::string& app_id) override {
    ++stop_count;
    if (app_id == "local_media") {
      local_media_running.store(false);
      return {true, false, "本地播放器已停止"};
    }
    if (app_id == "phone_projection") {
      phone_projection_running.store(false);
      return {true, false, "手机互联已停止"};
    }
    return {false, false, "未知应用"};
  }

  std::atomic<int> launch_count{0};
  std::atomic<int> stop_count{0};
  std::atomic_bool fail_phone_launch{false};
  std::atomic_bool local_media_running{false};
  std::atomic_bool phone_projection_running{false};
};

int FindRow(const AppLauncherModel& model, const QString& app_id) {
  for (int row = 0; row < model.rowCount(); ++row) {
    if (model.data(model.index(row), AppLauncherModel::AppIdRole).toString() == app_id) {
      return row;
    }
  }
  return -1;
}

}  // namespace

class AppLauncherModelTest final : public QObject {
  Q_OBJECT

 private slots:
  void EnforcesAllowlistAndLifecycle() {
    auto backend = std::make_unique<FakeAppLauncherBackend>();
    FakeAppLauncherBackend* backend_observer = backend.get();
    AppLauncherModel model(std::move(backend));
    QCOMPARE(model.rowCount(), 3);
    const int media_row = FindRow(model, QStringLiteral("local_media"));
    const int projection_row = FindRow(model, QStringLiteral("phone_projection"));
    const int android_row = FindRow(model, QStringLiteral("android_apps"));
    QVERIFY(media_row >= 0);
    QVERIFY(projection_row >= 0);
    QVERIFY(android_row >= 0);

    model.Start();
    QTRY_VERIFY_WITH_TIMEOUT(
        model.data(model.index(media_row), AppLauncherModel::AvailableRole).toBool(), 2000);
    QCOMPARE(model.data(model.index(android_row), AppLauncherModel::StateRole).toString(),
             QStringLiteral("UNAVAILABLE"));

    model.launch(QStringLiteral("../../bin/sh -c unsafe"));
    QCOMPARE(backend_observer->launch_count.load(), 0);
    QCOMPARE(model.lastError(), QStringLiteral("应用不在允许列表中"));

    model.launch(QStringLiteral("local_media"));
    QCOMPARE(model.data(model.index(media_row), AppLauncherModel::StateRole).toString(),
             QStringLiteral("STARTING"));
    model.launch(QStringLiteral("local_media"));
    QTRY_COMPARE_WITH_TIMEOUT(backend_observer->launch_count.load(), 1, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(
        model.data(model.index(media_row), AppLauncherModel::StateRole).toString(),
        QStringLiteral("RUNNING"), 2000);

    model.stop(QStringLiteral("local_media"));
    QTRY_COMPARE_WITH_TIMEOUT(backend_observer->stop_count.load(), 1, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(
        model.data(model.index(media_row), AppLauncherModel::StateRole).toString(),
        QStringLiteral("AVAILABLE"), 2000);

    backend_observer->fail_phone_launch.store(true);
    model.launch(QStringLiteral("phone_projection"));
    QTRY_COMPARE_WITH_TIMEOUT(
        model.data(model.index(projection_row), AppLauncherModel::StateRole).toString(),
        QStringLiteral("FAILED"), 2000);
    QCOMPARE(model.lastError(), QStringLiteral("手机互联启动失败"));

    backend_observer->fail_phone_launch.store(false);
    model.launch(QStringLiteral("phone_projection"));
    QTRY_COMPARE_WITH_TIMEOUT(
        model.data(model.index(projection_row), AppLauncherModel::StateRole).toString(),
        QStringLiteral("RUNNING"), 2000);
    model.Stop();
    QCOMPARE(backend_observer->stop_count.load(), 2);
    QVERIFY(!backend_observer->phone_projection_running.load());
  }

  void DefaultBackendStaysUnavailable() {
    AppLauncherModel model;
    const int media_row = FindRow(model, QStringLiteral("local_media"));
    QVERIFY(media_row >= 0);
    model.Start();
    QTRY_VERIFY_WITH_TIMEOUT(
        !model.data(model.index(media_row), AppLauncherModel::MessageRole).toString().isEmpty(),
        2000);
    QVERIFY(!model.data(model.index(media_row), AppLauncherModel::AvailableRole).toBool());
    QCOMPARE(model.data(model.index(media_row), AppLauncherModel::StateRole).toString(),
             QStringLiteral("UNAVAILABLE"));
    model.Stop();
  }
};

}  // namespace ui
}  // namespace cockpit

QTEST_GUILESS_MAIN(cockpit::ui::AppLauncherModelTest)

#include "app_launcher_model_test.moc"
