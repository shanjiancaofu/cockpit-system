#include "cockpit/library/transfer/transfer_runtime.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <string>

#include "cockpit/core/config/system_config.h"
#include "cockpit/core/logging/logger.h"

namespace cockpit {
namespace transfer {

TransferRuntime::~TransferRuntime() {
  Stop();
}

bool TransferRuntime::Start(const std::string& config_path, int samples, int max_hz) {
  if (running_.load() || max_hz <= 0) {
    return false;
  }

  try {
    const auto system_config = config::SystemConfig::LoadFromFile(config_path);
    const auto& gateway_config = system_config.services().gateway;
    logging::InitLogger("transfer", system_config.paths().log_dir,
                        logging::ParseLevel(system_config.logging().level),
                        system_config.logging().max_bytes, system_config.logging().mirror_stderr);
    if (!gateway_service_.Start(gateway_config.grpc.listen_address, std::max(1, 1000 / max_hz))) {
      return false;
    }
    vehicle_client_ = std::make_unique<gateway::VehicleStateClient>(
        gateway_config.vehicle_data_address, gateway_config.stream_timeout_ms,
        gateway_config.retry_delay_ms);
    LOG_INFO("transfer listen plan grpc_address=" + gateway_config.grpc.listen_address +
             " websocket_address=" + gateway_config.websocket.listen_address);
  } catch (const std::exception& error) {
    LOG_ERROR("failed to configure transfer: " + std::string(error.what()));
    gateway_service_.Shutdown();
    return false;
  }

  stopping_.store(false);
  result_.store(0);
  running_.store(true);
  worker_ = std::thread([this, samples, max_hz] {
    int stream_result = vehicle_client_->Stream(
        samples, max_hz,
        [this](const proto::vehicle::VehicleState& state) {
          gateway_service_.PublishVehicleState(state);
        },
        [this] {
          return !stopping_.load();
        });
    if (!stopping_.load() && stream_result == 0) {
      stream_result = 1;
    }
    result_.store(stream_result);
    running_.store(false);
  });
  return true;
}

void TransferRuntime::Stop() {
  stopping_.store(true);
  if (worker_.joinable()) {
    worker_.join();
  }
  gateway_service_.Shutdown();
  vehicle_client_.reset();
  running_.store(false);
}

int TransferRuntime::Poll() const {
  if (running_.load()) {
    return 0;
  }
  const int result = result_.load();
  return result == 0 ? 1 : result;
}

}  // namespace transfer
}  // namespace cockpit
