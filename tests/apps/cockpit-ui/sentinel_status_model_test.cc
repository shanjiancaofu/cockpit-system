#include "cockpit/apps/cockpit-ui/sentinel/sentinel_status_model.h"

#include <grpcpp/grpcpp.h>

#include <QtTest>
#include <memory>
#include <string>

#include "common.pb.h"
#include "sentinel.grpc.pb.h"

namespace cockpit {
namespace ui {
namespace {

class FakeSentinelControl final : public proto::sentinel::SentinelControl::Service {
 public:
  grpc::Status Arm(grpc::ServerContext*, const proto::common::Empty*,
                   proto::sentinel::SentinelStatus* response) override {
    state_ = proto::sentinel::SENTINEL_STATE_ARMED;
    return GetStatus(nullptr, nullptr, response);
  }
  grpc::Status Disarm(grpc::ServerContext*, const proto::common::Empty*,
                      proto::sentinel::SentinelStatus* response) override {
    state_ = proto::sentinel::SENTINEL_STATE_DISABLED;
    return GetStatus(nullptr, nullptr, response);
  }
  grpc::Status GetStatus(grpc::ServerContext*, const proto::common::Empty*,
                         proto::sentinel::SentinelStatus* response) override {
    response->set_state(state_);
    response->set_accepted_events(2);
    response->set_suppressed_events(3);
    response->set_last_snapshot_path("/tmp/sentinel.jpg");
    return grpc::Status::OK;
  }
  proto::sentinel::SentinelState state_ = proto::sentinel::SENTINEL_STATE_DISABLED;
};

}  // namespace

class SentinelStatusModelTest final : public QObject {
  Q_OBJECT
 private slots:
  void PollsAndControlsArming() {
    FakeSentinelControl service;
    grpc::ServerBuilder builder;
    int port = 0;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
    builder.RegisterService(&service);
    auto server = builder.BuildAndStart();
    QVERIFY(server != nullptr);

    SentinelStatusModel model("127.0.0.1:" + std::to_string(port));
    model.Start();
    QTRY_VERIFY_WITH_TIMEOUT(model.connected(), 2000);
    QCOMPARE(model.state(), QStringLiteral("DISABLED"));
    QCOMPARE(model.acceptedEvents(), 2ULL);
    QCOMPARE(model.suppressedEvents(), 3ULL);
    QCOMPARE(model.lastSnapshotPath(), QStringLiteral("/tmp/sentinel.jpg"));
    model.setArmed(true);
    QTRY_VERIFY_WITH_TIMEOUT(model.armed(), 2000);
    QCOMPARE(model.state(), QStringLiteral("ARMED"));
    model.setArmed(false);
    QTRY_VERIFY_WITH_TIMEOUT(!model.armed(), 2000);
    QCOMPARE(model.state(), QStringLiteral("DISABLED"));
    server->Shutdown();
    server.reset();
    QTRY_VERIFY_WITH_TIMEOUT(!model.connected(), 2000);
    model.Stop();
  }
};

}  // namespace ui
}  // namespace cockpit

QTEST_GUILESS_MAIN(cockpit::ui::SentinelStatusModelTest)
#include "sentinel_status_model_test.moc"
