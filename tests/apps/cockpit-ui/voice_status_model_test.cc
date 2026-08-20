#include "cockpit/apps/cockpit-ui/voice/voice_status_model.h"

#include <grpcpp/grpcpp.h>

#include <QtTest>
#include <atomic>
#include <memory>
#include <string>

#include "common.pb.h"
#include "voice.grpc.pb.h"

namespace cockpit {
namespace ui {
namespace {

class FakeVoiceService final : public proto::voice::VoiceInteractionControl::Service {
 public:
  grpc::Status GetStatus(grpc::ServerContext*, const proto::common::Empty*,
                         proto::voice::VoiceInteractionStatus* response) override {
    response->set_state(static_cast<proto::voice::InteractionState>(state_.load()));
    response->set_state_reason("test state reason");
    response->mutable_metrics()->set_audio_playback_available(true);
    auto* latest = response->mutable_latest_response();
    latest->set_id(7);
    latest->set_transcript_text("打开相机");
    latest->set_response_text("正在打开相机");
    latest->set_action("open_camera");
    return grpc::Status::OK;
  }

  grpc::Status Interrupt(grpc::ServerContext*, const proto::common::Empty*,
                         proto::voice::InterruptVoiceResponse* response) override {
    ++interrupt_count_;
    response->set_active_request_interrupted(true);
    state_.store(proto::voice::INTERACTION_STATE_IDLE);
    return grpc::Status::OK;
  }

  std::atomic<int> state_{proto::voice::INTERACTION_STATE_LISTENING};
  std::atomic<int> interrupt_count_{0};
};

}  // namespace

class VoiceStatusModelTest final : public QObject {
  Q_OBJECT

 private slots:
  void PollsStatusAndInterruptsActiveRequest() {
    FakeVoiceService service;
    grpc::ServerBuilder builder;
    int port = 0;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
    QVERIFY(server != nullptr);
    QVERIFY(port > 0);

    VoiceStatusModel model("127.0.0.1:" + std::to_string(port));
    model.Start();
    QTRY_VERIFY_WITH_TIMEOUT(model.connected(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(model.state(), QStringLiteral("LISTENING"), 2000);
    QCOMPARE(model.stateLabel(), QStringLiteral("正在聆听"));
    QCOMPARE(model.stateReason(), QStringLiteral("test state reason"));
    QCOMPARE(model.transcriptText(), QStringLiteral("打开相机"));
    QCOMPARE(model.responseText(), QStringLiteral("正在打开相机"));
    QCOMPARE(model.actionText(), QStringLiteral("open_camera"));
    QVERIFY(model.active());
    QVERIFY(model.canInterrupt());
    QVERIFY(model.playbackAvailable());

    model.interrupt();
    QVERIFY(model.interruptPending());
    QTRY_COMPARE_WITH_TIMEOUT(service.interrupt_count_.load(), 1, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(!model.interruptPending(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(model.state(), QStringLiteral("IDLE"), 2000);
    QVERIFY(!model.active());
    QVERIFY(!model.canInterrupt());

    server->Shutdown();
    server.reset();
    QTRY_VERIFY_WITH_TIMEOUT(!model.connected(), 2000);
    QCOMPARE(model.state(), QStringLiteral("DISCONNECTED"));
    QVERIFY(!model.lastError().isEmpty());
    model.Stop();
  }
};

}  // namespace ui
}  // namespace cockpit

QTEST_GUILESS_MAIN(cockpit::ui::VoiceStatusModelTest)

#include "voice_status_model_test.moc"
