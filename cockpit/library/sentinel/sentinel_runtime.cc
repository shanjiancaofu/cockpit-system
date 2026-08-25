#include "cockpit/library/sentinel/sentinel_runtime.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <thread>

#include "cockpit/core/config/system_config.h"
#include "cockpit/core/logging/logger.h"
#include "cockpit/library/sentinel/chassis_event_client.h"
#include "cockpit/library/sentinel/sentinel_actions.h"
#include "cockpit/library/sentinel/sentinel_grpc_service.h"
#include "cockpit/modules/sentinel/sentinel_service.h"

namespace cockpit {
namespace sentinel {

class SentinelRuntime::Impl {
 public:
  std::unique_ptr<SentinelService> service;
  std::unique_ptr<SentinelGrpcService> grpc;
  std::unique_ptr<ChassisEventClient> events;
  std::atomic_bool stopping{false};
  std::atomic_int result{0};
  std::thread worker;
};

SentinelRuntime::SentinelRuntime() = default;
SentinelRuntime::~SentinelRuntime() {
  Stop();
}

bool SentinelRuntime::Start(const std::string& config_path) {
  if (impl_ != nullptr) return false;
  try {
    const auto config = config::SystemConfig::LoadFromFile(config_path);
    const auto& sentinel_config = config.services().sentinel;
    logging::InitLogger("sentinel", config.paths().log_dir,
                        logging::ParseLevel(config.logging().level), config.logging().mirror_stderr,
                        config.logging().dump_time_secs, config.logging().cut_off_time_mins,
                        config.logging().max_files);
    SentinelPolicy policy;
    policy.cooldown = std::chrono::milliseconds(sentinel_config.cooldown_ms);
    policy.max_event_age = std::chrono::milliseconds(sentinel_config.max_event_age_ms);
    policy.queue_capacity = static_cast<std::size_t>(sentinel_config.queue_capacity);
    auto actions = std::make_unique<GrpcSentinelActions>(
        config.services().camera.grpc.listen_address,
        config.services().recording.grpc.listen_address, sentinel_config.rpc_timeout_ms);
    impl_ = std::make_unique<Impl>();
    impl_->service = std::make_unique<SentinelService>(policy, std::move(actions));
    if (!impl_->service->Start(sentinel_config.auto_arm)) {
      impl_.reset();
      return false;
    }
    impl_->grpc = std::make_unique<SentinelGrpcService>(*impl_->service);
    if (!impl_->grpc->Start(sentinel_config.grpc.listen_address)) {
      Stop();
      return false;
    }
    impl_->events = std::make_unique<ChassisEventClient>(sentinel_config.vehicle_data_address);
    impl_->worker = std::thread([this] {
      impl_->result.store(impl_->events->Stream(
          [this](vehicle::ChassisEvent event) {
            return impl_->service->Submit(std::move(event));
          },
          [this] {
            return !impl_->stopping.load();
          }));
    });
    return true;
  } catch (const std::exception& error) {
    LOG_ERROR("failed to configure sentinel: " + std::string(error.what()));
    Stop();
    return false;
  }
}

void SentinelRuntime::Stop() {
  if (impl_ == nullptr) return;
  impl_->stopping.store(true);
  if (impl_->events != nullptr) impl_->events->Stop();
  if (impl_->service != nullptr) impl_->service->Stop();
  if (impl_->worker.joinable()) impl_->worker.join();
  if (impl_->grpc != nullptr) impl_->grpc->Shutdown();
  impl_.reset();
}

int SentinelRuntime::Poll() const {
  return impl_ == nullptr ? 1 : impl_->result.load();
}

}  // namespace sentinel
}  // namespace cockpit
