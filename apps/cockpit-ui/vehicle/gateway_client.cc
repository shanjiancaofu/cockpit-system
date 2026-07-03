#include "gateway_client.h"

#include <QMetaObject>
#include <QString>
#include <chrono>
#include <utility>

#include "vehicle_state_model.h"

#include "core/logging/Logger.h"
#include "gateway.grpc.pb.h"

namespace cockpit {
namespace ui {

GatewayClient::GatewayClient(std::string address, VehicleStateModel* model)
    : address_(std::move(address)), model_(model) {
}

GatewayClient::~GatewayClient() {
  Stop();
}

void GatewayClient::Start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true)) {
    return;
  }
  worker_ = std::thread(&GatewayClient::Run, this);
}

void GatewayClient::Stop() {
  running_.store(false);
  {
    std::lock_guard<std::mutex> lock(context_mutex_);
    if (active_context_ != nullptr) {
      active_context_->TryCancel();
    }
  }
  if (worker_.joinable()) {
    worker_.join();
  }
}

void GatewayClient::Run() {
  grpc::ChannelArguments arguments;
  arguments.SetInt(GRPC_ARG_ENABLE_HTTP_PROXY, 0);
  auto channel = grpc::CreateCustomChannel(address_, grpc::InsecureChannelCredentials(), arguments);
  auto stub = proto::gateway::CockpitGateway::NewStub(channel);

  while (running_.load()) {
    grpc::ClientContext context;
    context.set_wait_for_ready(true);
    {
      std::lock_guard<std::mutex> lock(context_mutex_);
      active_context_ = &context;
    }

    proto::gateway::SubscribeCockpitEventsRequest request;
    request.set_client_id("cockpit-ui");
    request.set_max_hz(30);
    auto reader = stub->SubscribeCockpitEvents(&context, request);

    bool received = false;
    proto::gateway::CockpitEvent event;
    while (running_.load() && reader->Read(&event)) {
      if (!event.has_vehicle_state()) {
        continue;
      }
      if (!received) {
        received = true;
        PostConnected(true);
      }
      const auto& state = event.vehicle_state();
      const qint64 timestamp_ms = state.timestamp_ms();
      const double speed_kph = state.speed_kph();
      const int gear = state.gear();
      const int soc_percent = state.soc_percent();
      const bool cloud_enabled = state.cloud_enabled();
      const QString source = QString::fromStdString(state.source());
      QMetaObject::invokeMethod(
          model_,
          [model = model_, timestamp_ms, speed_kph, gear, soc_percent, cloud_enabled, source] {
            model->Update(timestamp_ms, speed_kph, gear, soc_percent, cloud_enabled, source);
          },
          Qt::QueuedConnection);
    }

    const grpc::Status status = reader->Finish();
    {
      std::lock_guard<std::mutex> lock(context_mutex_);
      active_context_ = nullptr;
    }
    PostConnected(false);
    if (!running_.load()) {
      break;
    }
    LOG_WARN("cockpit UI gateway stream interrupted grpc_code=" +
             std::to_string(status.error_code()) + " message=" + status.error_message());
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
}

void GatewayClient::PostConnected(bool connected) {
  QMetaObject::invokeMethod(
      model_,
      [model = model_, connected] {
        model->SetConnected(connected);
      },
      Qt::QueuedConnection);
}

}  // namespace ui
}  // namespace cockpit
