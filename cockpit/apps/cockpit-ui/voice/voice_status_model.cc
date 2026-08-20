#include "cockpit/apps/cockpit-ui/voice/voice_status_model.h"

#include <grpcpp/grpcpp.h>

#include <QMetaObject>
#include <chrono>
#include <utility>

#include "common.pb.h"
#include "voice.grpc.pb.h"

namespace cockpit {
namespace ui {
namespace {

constexpr auto kPollInterval = std::chrono::milliseconds(200);
constexpr auto kRpcTimeout = std::chrono::milliseconds(400);

struct UiState {
  QString key;
  QString label;
  bool active = false;
};

UiState MapState(proto::voice::InteractionState state) {
  switch (state) {
    case proto::voice::INTERACTION_STATE_DISABLED:
      return {QStringLiteral("DISABLED"), QStringLiteral("语音已关闭"), false};
    case proto::voice::INTERACTION_STATE_IDLE:
      return {QStringLiteral("IDLE"), QStringLiteral("等待唤醒"), false};
    case proto::voice::INTERACTION_STATE_WAKING:
      return {QStringLiteral("WAKING"), QStringLiteral("正在唤醒"), true};
    case proto::voice::INTERACTION_STATE_LISTENING:
      return {QStringLiteral("LISTENING"), QStringLiteral("正在聆听"), true};
    case proto::voice::INTERACTION_STATE_RECOGNIZING:
      return {QStringLiteral("RECOGNIZING"), QStringLiteral("正在识别"), true};
    case proto::voice::INTERACTION_STATE_ROUTING:
      return {QStringLiteral("ROUTING"), QStringLiteral("正在理解"), true};
    case proto::voice::INTERACTION_STATE_EXECUTING:
      return {QStringLiteral("EXECUTING"), QStringLiteral("正在执行"), true};
    case proto::voice::INTERACTION_STATE_THINKING:
      return {QStringLiteral("THINKING"), QStringLiteral("正在思考"), true};
    case proto::voice::INTERACTION_STATE_SPEAKING:
      return {QStringLiteral("SPEAKING"), QStringLiteral("正在播报"), true};
    case proto::voice::INTERACTION_STATE_FOLLOW_UP:
      return {QStringLiteral("FOLLOW_UP"), QStringLiteral("可以继续说"), true};
    case proto::voice::INTERACTION_STATE_CANCELLED:
      return {QStringLiteral("CANCELLED"), QStringLiteral("已取消"), false};
    case proto::voice::INTERACTION_STATE_ERROR_RECOVERY:
      return {QStringLiteral("ERROR_RECOVERY"), QStringLiteral("正在恢复"), false};
    case proto::voice::INTERACTION_STATE_SHUTTING_DOWN:
      return {QStringLiteral("SHUTTING_DOWN"), QStringLiteral("服务停止中"), false};
    case proto::voice::INTERACTION_STATE_PROCESSING:
      return {QStringLiteral("PROCESSING"), QStringLiteral("正在处理"), true};
    case proto::voice::INTERACTION_STATE_FAULTED:
      return {QStringLiteral("FAULTED"), QStringLiteral("服务故障"), false};
    case proto::voice::INTERACTION_STATE_UNSPECIFIED:
    default:
      return {QStringLiteral("UNKNOWN"), QStringLiteral("状态未知"), false};
  }
}

void SetDeadline(grpc::ClientContext* context) {
  context->set_deadline(std::chrono::system_clock::now() + kRpcTimeout);
}

QString RpcError(const grpc::Status& status) {
  if (status.ok()) {
    return {};
  }
  const QString message = QString::fromStdString(status.error_message());
  return message.isEmpty() ? QStringLiteral("voice service unavailable") : message;
}

}  // namespace

VoiceStatusModel::VoiceStatusModel(std::string address, QObject* parent)
    : QObject(parent), address_(std::move(address)) {
}

VoiceStatusModel::~VoiceStatusModel() {
  Stop();
}

void VoiceStatusModel::Start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true)) {
    return;
  }
  worker_ = std::thread(&VoiceStatusModel::Run, this);
}

void VoiceStatusModel::Stop() {
  running_.store(false);
  wait_condition_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void VoiceStatusModel::interrupt() {
  if (!can_interrupt_ || interrupt_pending_) {
    return;
  }
  interrupt_pending_ = true;
  interrupt_requested_.store(true);
  emit statusChanged();
  wait_condition_.notify_all();
}

void VoiceStatusModel::Run() {
  grpc::ChannelArguments arguments;
  arguments.SetInt(GRPC_ARG_ENABLE_HTTP_PROXY, 0);
  auto channel = grpc::CreateCustomChannel(address_, grpc::InsecureChannelCredentials(), arguments);
  auto stub = proto::voice::VoiceInteractionControl::NewStub(channel);

  while (running_.load()) {
    if (interrupt_requested_.exchange(false)) {
      proto::common::Empty request;
      proto::voice::InterruptVoiceResponse response;
      grpc::ClientContext context;
      SetDeadline(&context);
      const grpc::Status status = stub->Interrupt(&context, request, &response);
      QMetaObject::invokeMethod(
          this,
          [this, response = std::move(response), error = RpcError(status)] {
            ApplyInterruptResult(response, error);
          },
          Qt::QueuedConnection);
    }

    proto::common::Empty request;
    proto::voice::VoiceInteractionStatus response;
    grpc::ClientContext context;
    SetDeadline(&context);
    const grpc::Status status = stub->GetStatus(&context, request, &response);
    if (status.ok()) {
      QMetaObject::invokeMethod(
          this,
          [this, response = std::move(response)] {
            ApplyStatus(response);
          },
          Qt::QueuedConnection);
    } else {
      QMetaObject::invokeMethod(
          this,
          [this, error = RpcError(status)] {
            ApplyDisconnected(error);
          },
          Qt::QueuedConnection);
    }

    std::unique_lock<std::mutex> lock(wait_mutex_);
    wait_condition_.wait_for(lock, kPollInterval, [this] {
      return !running_.load() || interrupt_requested_.load();
    });
  }
}

void VoiceStatusModel::ApplyStatus(const proto::voice::VoiceInteractionStatus& status) {
  const UiState mapped = MapState(status.state());
  connected_ = true;
  active_ = mapped.active;
  can_interrupt_ = mapped.active;
  playback_available_ = status.metrics().audio_playback_available();
  state_ = mapped.key;
  state_label_ = mapped.label;
  state_reason_ = QString::fromStdString(status.state_reason());
  last_error_ = QString::fromStdString(status.last_error());
  if (status.has_latest_response()) {
    transcript_text_ = QString::fromStdString(status.latest_response().transcript_text());
    response_text_ = QString::fromStdString(status.latest_response().response_text());
    action_text_ = QString::fromStdString(status.latest_response().action());
  }
  emit statusChanged();
}

void VoiceStatusModel::ApplyDisconnected(QString error) {
  connected_ = false;
  active_ = false;
  can_interrupt_ = false;
  interrupt_pending_ = false;
  playback_available_ = false;
  state_ = QStringLiteral("DISCONNECTED");
  state_label_ = QStringLiteral("服务未连接");
  state_reason_ = QStringLiteral("后台将自动重试");
  last_error_ = std::move(error);
  emit statusChanged();
}

void VoiceStatusModel::ApplyInterruptResult(const proto::voice::InterruptVoiceResponse& response,
                                            QString error) {
  interrupt_pending_ = false;
  if (!error.isEmpty()) {
    last_error_ = std::move(error);
  } else if (!response.active_request_interrupted() &&
             response.queued_transcripts_discarded() == 0) {
    last_error_ = QStringLiteral("当前没有可取消的语音请求");
  } else {
    last_error_.clear();
  }
  emit statusChanged();
}

}  // namespace ui
}  // namespace cockpit
