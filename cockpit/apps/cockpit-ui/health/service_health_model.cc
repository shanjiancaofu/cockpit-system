#include "service_health_model.h"

#include <grpcpp/grpcpp.h>

#include <QMetaObject>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>
#include <utility>

#include "audio.grpc.pb.h"
#include "camera.grpc.pb.h"
#include "common.pb.h"
#include "gateway.grpc.pb.h"
#include "recording.grpc.pb.h"
#include "vehicle_state.pb.h"
#include "voice.grpc.pb.h"

namespace cockpit {
namespace ui {
namespace {

constexpr int kPollIntervalMs = 1000;
constexpr int kRpcDeadlineMs = 350;

std::int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void PrepareContext(grpc::ClientContext* context) {
  context->set_deadline(std::chrono::system_clock::now() +
                        std::chrono::milliseconds(kRpcDeadlineMs));
}

QString StateName(proto::common::ServiceHealthState state) {
  switch (state) {
    case proto::common::SERVICE_HEALTH_STATE_OK:
      return QStringLiteral("OK");
    case proto::common::SERVICE_HEALTH_STATE_DEGRADED:
      return QStringLiteral("DEGRADED");
    case proto::common::SERVICE_HEALTH_STATE_FAULTED:
      return QStringLiteral("FAULTED");
    case proto::common::SERVICE_HEALTH_STATE_UNSPECIFIED:
    default:
      return QStringLiteral("UNKNOWN");
  }
}

ServiceHealthModel::HealthSample FromHealth(const proto::common::ServiceHealth& health) {
  ServiceHealthModel::HealthSample sample;
  sample.state = StateName(health.state());
  sample.message = QString::fromStdString(health.message());
  sample.last_error = QString::fromStdString(health.last_error());
  sample.checked_at_ms = health.checked_at_ms();
  if (sample.checked_at_ms == 0) {
    sample.checked_at_ms = NowMs();
  }
  if (sample.message.isEmpty()) {
    sample.message = sample.state == QStringLiteral("OK") ? QStringLiteral("Online")
                                                          : QStringLiteral("No health message");
  }
  return sample;
}

ServiceHealthModel::HealthSample RpcFailure(const grpc::Status& status) {
  ServiceHealthModel::HealthSample sample;
  sample.state = QStringLiteral("UNKNOWN");
  sample.checked_at_ms = NowMs();
  sample.last_error = QString::fromStdString(status.error_message());
  if (sample.last_error.isEmpty()) {
    sample.last_error = QStringLiteral("gRPC code %1").arg(status.error_code());
  }
  sample.message = QStringLiteral("Service unreachable");
  return sample;
}

}  // namespace

ServiceHealthModel::ServiceHealthModel(std::vector<ServiceHealthEndpoint> endpoints,
                                       QObject* parent)
    : QAbstractListModel(parent) {
  items_.reserve(endpoints.size());
  for (auto& endpoint : endpoints) {
    Item item;
    item.display_name = std::move(endpoint.display_name);
    item.service_name = std::move(endpoint.service_name);
    item.address = std::move(endpoint.address);
    items_.push_back(std::move(item));
  }
}

ServiceHealthModel::~ServiceHealthModel() {
  Stop();
}

int ServiceHealthModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<int>(items_.size());
}

QVariant ServiceHealthModel::data(const QModelIndex& index, int role) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(items_.size())) {
    return {};
  }
  const Item& item = items_[static_cast<std::size_t>(index.row())];
  switch (role) {
    case DisplayNameRole:
      return item.display_name;
    case ServiceNameRole:
      return QString::fromStdString(item.service_name);
    case StateRole:
      return item.state;
    case MessageRole:
      return item.message;
    case LastErrorRole:
      return item.last_error;
    case CheckedAtRole:
      return item.checked_at_ms;
    default:
      break;
  }
  return {};
}

QHash<int, QByteArray> ServiceHealthModel::roleNames() const {
  return {
      {DisplayNameRole, "displayName"}, {ServiceNameRole, "serviceName"},
      {StateRole, "healthState"},       {MessageRole, "message"},
      {LastErrorRole, "lastError"},     {CheckedAtRole, "checkedAtMs"},
  };
}

int ServiceHealthModel::okCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  RecountLocked();
  return ok_count_;
}

int ServiceHealthModel::degradedCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  RecountLocked();
  return degraded_count_;
}

int ServiceHealthModel::faultedCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  RecountLocked();
  return faulted_count_;
}

int ServiceHealthModel::unknownCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  RecountLocked();
  return unknown_count_;
}

QString ServiceHealthModel::summaryText() const {
  std::lock_guard<std::mutex> lock(mutex_);
  RecountLocked();
  return QStringLiteral("%1 OK  %2 DEGRADED  %3 FAULTED  %4 UNKNOWN")
      .arg(ok_count_)
      .arg(degraded_count_)
      .arg(faulted_count_)
      .arg(unknown_count_);
}

QString ServiceHealthModel::worstState() const {
  std::lock_guard<std::mutex> lock(mutex_);
  RecountLocked();
  if (faulted_count_ > 0) {
    return QStringLiteral("FAULTED");
  }
  if (degraded_count_ > 0) {
    return QStringLiteral("DEGRADED");
  }
  if (unknown_count_ > 0) {
    return QStringLiteral("UNKNOWN");
  }
  return QStringLiteral("OK");
}

void ServiceHealthModel::Start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true)) {
    return;
  }
  worker_ = std::thread(&ServiceHealthModel::Run, this);
}

void ServiceHealthModel::Stop() {
  running_.store(false);
  if (worker_.joinable()) {
    worker_.join();
  }
}

void ServiceHealthModel::Run() {
  while (running_.load()) {
    PollOnce();
    for (int elapsed_ms = 0; running_.load() && elapsed_ms < kPollIntervalMs; elapsed_ms += 50) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }
}

void ServiceHealthModel::PollOnce() {
  grpc::ChannelArguments arguments;
  arguments.SetInt(GRPC_ARG_ENABLE_HTTP_PROXY, 0);

  std::vector<HealthSample> samples;
  std::vector<Item> snapshot;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot = items_;
  }
  samples.reserve(snapshot.size());

  for (const auto& item : snapshot) {
    auto channel =
        grpc::CreateCustomChannel(item.address, grpc::InsecureChannelCredentials(), arguments);
    if (item.service_name == "cockpit-gateway-service") {
      auto stub = proto::gateway::CockpitGateway::NewStub(channel);
      proto::common::Empty request;
      proto::gateway::GatewayStatus response;
      grpc::ClientContext context;
      PrepareContext(&context);
      const grpc::Status status = stub->GetStatus(&context, request, &response);
      samples.push_back(status.ok() ? FromHealth(response.health()) : RpcFailure(status));
      continue;
    }
    if (item.service_name == "audio-service") {
      auto stub = proto::audio::AudioControl::NewStub(channel);
      proto::common::Empty request;
      proto::audio::AudioStatus response;
      grpc::ClientContext context;
      PrepareContext(&context);
      const grpc::Status status = stub->GetStatus(&context, request, &response);
      samples.push_back(status.ok() ? FromHealth(response.health()) : RpcFailure(status));
      continue;
    }
    if (item.service_name == "voice-interaction-service") {
      auto stub = proto::voice::VoiceInteractionControl::NewStub(channel);
      proto::common::Empty request;
      proto::voice::VoiceInteractionStatus response;
      grpc::ClientContext context;
      PrepareContext(&context);
      const grpc::Status status = stub->GetStatus(&context, request, &response);
      samples.push_back(status.ok() ? FromHealth(response.health()) : RpcFailure(status));
      continue;
    }
    if (item.service_name == "camera-service") {
      auto stub = proto::camera::CameraControl::NewStub(channel);
      proto::common::Empty request;
      proto::camera::CameraStatus response;
      grpc::ClientContext context;
      PrepareContext(&context);
      const grpc::Status status = stub->GetStatus(&context, request, &response);
      samples.push_back(status.ok() ? FromHealth(response.health()) : RpcFailure(status));
      continue;
    }
    if (item.service_name == "recording-service") {
      auto stub = proto::recording::RecordingControl::NewStub(channel);
      proto::common::Empty request;
      proto::recording::RecordingStatus response;
      grpc::ClientContext context;
      PrepareContext(&context);
      const grpc::Status status = stub->GetStatus(&context, request, &response);
      samples.push_back(status.ok() ? FromHealth(response.health()) : RpcFailure(status));
      continue;
    }
    HealthSample sample;
    sample.state = QStringLiteral("UNKNOWN");
    sample.message = QStringLiteral("No health adapter");
    sample.checked_at_ms = NowMs();
    samples.push_back(std::move(sample));
  }

  PostSamples(std::move(samples));
}

void ServiceHealthModel::PostSamples(std::vector<HealthSample> samples) {
  QMetaObject::invokeMethod(
      this,
      [this, samples = std::move(samples)]() mutable {
        const int changed_rows = static_cast<int>(std::min(samples.size(), items_.size()));
        {
          std::lock_guard<std::mutex> lock(mutex_);
          for (int i = 0; i < changed_rows; ++i) {
            Item& item = items_[static_cast<std::size_t>(i)];
            HealthSample& sample = samples[static_cast<std::size_t>(i)];
            item.state = std::move(sample.state);
            item.message = std::move(sample.message);
            item.last_error = std::move(sample.last_error);
            item.checked_at_ms = sample.checked_at_ms;
          }
          counts_dirty_ = true;
        }
        if (changed_rows > 0) {
          emit dataChanged(index(0), index(changed_rows - 1));
        }
        emit summaryChanged();
      },
      Qt::QueuedConnection);
}

void ServiceHealthModel::RecountLocked() const {
  if (!counts_dirty_) {
    return;
  }
  ok_count_ = 0;
  degraded_count_ = 0;
  faulted_count_ = 0;
  unknown_count_ = 0;
  for (const auto& item : items_) {
    if (item.state == QStringLiteral("OK")) {
      ++ok_count_;
    } else if (item.state == QStringLiteral("DEGRADED")) {
      ++degraded_count_;
    } else if (item.state == QStringLiteral("FAULTED")) {
      ++faulted_count_;
    } else {
      ++unknown_count_;
    }
  }
  counts_dirty_ = false;
}

}  // namespace ui
}  // namespace cockpit
