#include "cockpit/library/bridge/bridge_runtime.h"

#include <chrono>
#include <exception>
#include <memory>
#include <string>

#include "cockpit/core/config/system_config.h"
#include "cockpit/core/logging/logger.h"
#include "cockpit/library/bridge/bridge_grpc_service.h"
#include "cockpit/modules/bridge/fake_bridge_provider.h"
#if COCKPIT_ENABLE_ROS2
#include "cockpit/library/bridge/ros2_nav2_provider.h"
#endif

namespace cockpit::bridge {

class BridgeRuntime::Impl {
 public:
  std::unique_ptr<BridgeService> service;
  std::unique_ptr<BridgeGrpcService> grpc;
};

BridgeRuntime::BridgeRuntime() = default;
BridgeRuntime::~BridgeRuntime() {
  Stop();
}

bool BridgeRuntime::Start(const std::string& config_path) {
  if (impl_ != nullptr) return false;
  try {
    const auto config = config::SystemConfig::LoadFromFile(config_path);
    const auto& bridge_config = config.services().bridge;
    logging::InitLogger("bridge", config.paths().log_dir,
                        logging::ParseLevel(config.logging().level), config.logging().mirror_stderr,
                        config.logging().dump_time_secs, config.logging().cut_off_time_mins,
                        config.logging().max_files);
    std::unique_ptr<NavigationProvider> provider;
    if (bridge_config.provider == "disabled") {
      provider = CreateDisabledNavigationProvider();
    } else if (bridge_config.provider == "fake") {
      FakeBridgeOutcome outcome;
      if (!ParseFakeBridgeOutcome(bridge_config.fake_outcome, &outcome)) return false;
      provider = CreateFakeNavigationProvider(outcome);
    } else if (bridge_config.provider == "ros2_nav2") {
#if COCKPIT_ENABLE_ROS2
      Ros2Nav2ProviderOptions options;
      options.action_name = bridge_config.nav2_action_name;
      options.server_timeout = std::chrono::milliseconds(bridge_config.nav2_server_timeout_ms);
      std::string error;
      provider = CreateRos2Nav2Provider(options, &error);
      if (provider == nullptr) {
        LOG_ERROR(error);
        return false;
      }
#else
      LOG_ERROR("bridge provider ros2_nav2 requires COCKPIT_ENABLE_ROS2=ON");
      return false;
#endif
    } else {
      return false;
    }
    impl_ = std::make_unique<Impl>();
    impl_->service =
        std::make_unique<BridgeService>(std::move(provider), bridge_config.goal_timeout_ms);
    impl_->grpc = std::make_unique<BridgeGrpcService>(*impl_->service);
    if (!impl_->grpc->Start(bridge_config.grpc.listen_address)) {
      impl_.reset();
      return false;
    }
    return true;
  } catch (const std::exception& error) {
    LOG_ERROR("failed to configure bridge: " + std::string(error.what()));
    Stop();
    return false;
  }
}

void BridgeRuntime::Stop() {
  if (impl_ == nullptr) return;
  if (impl_->grpc != nullptr) impl_->grpc->Shutdown();
  impl_.reset();
}

int BridgeRuntime::Poll() const {
  return impl_ == nullptr ? 1 : 0;
}

}  // namespace cockpit::bridge
