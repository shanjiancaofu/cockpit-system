#include "cockpit/apps/cockpit-ui/sentinel/sentinel_status_model.h"

#include <grpcpp/grpcpp.h>

#include <QMetaObject>
#include <chrono>
#include <utility>

#include "common.pb.h"
#include "sentinel.grpc.pb.h"

namespace cockpit {
namespace ui {
namespace {

constexpr auto kPollInterval = std::chrono::milliseconds(250);
constexpr auto kRpcTimeout = std::chrono::milliseconds(500);

void Deadline(grpc::ClientContext* context) {
  context->set_deadline(std::chrono::system_clock::now() + kRpcTimeout);
}

std::pair<QString, QString> StateText(proto::sentinel::SentinelState state) {
  switch (state) {
    case proto::sentinel::SENTINEL_STATE_DISABLED:
      return {"DISABLED", QStringLiteral("未布防")};
    case proto::sentinel::SENTINEL_STATE_ARMED:
      return {"ARMED", QStringLiteral("已布防")};
    case proto::sentinel::SENTINEL_STATE_TRIGGERED:
      return {"TRIGGERED", QStringLiteral("正在取证")};
    case proto::sentinel::SENTINEL_STATE_COOLDOWN:
      return {"COOLDOWN", QStringLiteral("冷却中")};
    case proto::sentinel::SENTINEL_STATE_FAULTED:
      return {"FAULTED", QStringLiteral("服务故障")};
    default:
      return {"UNKNOWN", QStringLiteral("状态未知")};
  }
}

}  // namespace

SentinelStatusModel::SentinelStatusModel(std::string address, QObject* parent)
    : QObject(parent), address_(std::move(address)) {
}
SentinelStatusModel::~SentinelStatusModel() {
  Stop();
}

void SentinelStatusModel::Start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true)) return;
  worker_ = std::thread(&SentinelStatusModel::Run, this);
}

void SentinelStatusModel::Stop() {
  running_.store(false);
  wait_condition_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void SentinelStatusModel::setArmed(bool armed) {
  if (!running_.load() || busy_) return;
  busy_ = true;
  command_.store(armed ? Command::kArm : Command::kDisarm);
  emit statusChanged();
  wait_condition_.notify_all();
}

void SentinelStatusModel::Run() {
  grpc::ChannelArguments arguments;
  arguments.SetInt(GRPC_ARG_ENABLE_HTTP_PROXY, 0);
  auto channel = grpc::CreateCustomChannel(address_, grpc::InsecureChannelCredentials(), arguments);
  auto stub = proto::sentinel::SentinelControl::NewStub(channel);
  while (running_.load()) {
    proto::common::Empty request;
    proto::sentinel::SentinelStatus response;
    grpc::ClientContext context;
    Deadline(&context);
    const Command command = command_.exchange(Command::kNone);
    grpc::Status status;
    if (command == Command::kArm)
      status = stub->Arm(&context, request, &response);
    else if (command == Command::kDisarm)
      status = stub->Disarm(&context, request, &response);
    else
      status = stub->GetStatus(&context, request, &response);
    const QString error = status.ok() ? QString() : QString::fromStdString(status.error_message());
    QMetaObject::invokeMethod(
        this,
        [this, response = std::move(response), error] {
          ApplyStatus(response, error);
        },
        Qt::QueuedConnection);
    std::unique_lock<std::mutex> lock(wait_mutex_);
    wait_condition_.wait_for(lock, kPollInterval, [this] {
      return !running_.load() || command_.load() != Command::kNone;
    });
  }
}

void SentinelStatusModel::ApplyStatus(const proto::sentinel::SentinelStatus& status,
                                      QString rpc_error) {
  busy_ = false;
  if (!rpc_error.isEmpty()) {
    connected_ = false;
    armed_ = false;
    state_ = QStringLiteral("DISCONNECTED");
    state_label_ = QStringLiteral("服务未连接");
    last_error_ = std::move(rpc_error);
    emit statusChanged();
    return;
  }
  connected_ = true;
  const auto text = StateText(status.state());
  state_ = text.first;
  state_label_ = text.second;
  armed_ = status.state() == proto::sentinel::SENTINEL_STATE_ARMED ||
           status.state() == proto::sentinel::SENTINEL_STATE_TRIGGERED ||
           status.state() == proto::sentinel::SENTINEL_STATE_COOLDOWN;
  accepted_events_ = status.accepted_events();
  suppressed_events_ = status.suppressed_events();
  last_snapshot_path_ = QString::fromStdString(status.last_snapshot_path());
  last_error_ = QString::fromStdString(status.last_error());
  emit statusChanged();
}

}  // namespace ui
}  // namespace cockpit
