#include "cockpit/library/driver/vehicle/vehicle_runtime.h"

#include <atomic>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "cockpit/core/config/system_config.h"
#include "cockpit/core/logging/logger.h"
#include "cockpit/library/driver/vehicle/vehicle_data_service.h"
#include "cockpit/library/driver/vehicle/vehicle_grpc_service.h"

namespace cockpit {
namespace vehicle {

class VehicleRuntime::Impl {
 public:
  VehicleGrpcService grpc;
  std::unique_ptr<VehicleDataService> service;
  std::thread worker;
  std::atomic_bool stopping{false};
  std::atomic_bool running{false};
  std::atomic_int result{0};
  bool persistent{true};
};

VehicleRuntime::VehicleRuntime() = default;

VehicleRuntime::~VehicleRuntime() {
  Stop();
}

bool VehicleRuntime::Start(const std::string& config_path, const std::string& source_override,
                           int samples, bool forever) {
  if (impl_ != nullptr || samples < 0) {
    return false;
  }
  try {
    const auto config = config::SystemConfig::LoadFromFile(config_path);
    logging::InitLogger("vehicle_driver", config.paths().log_dir,
                        logging::ParseLevel(config.logging().level), config.logging().mirror_stderr,
                        config.logging().dump_time_secs, config.logging().cut_off_time_mins,
                        config.logging().max_files);
    impl_ = std::make_unique<Impl>();
    if (!impl_->grpc.Start(config.services().vehicle_data.grpc.listen_address)) {
      impl_.reset();
      return false;
    }
    VehicleDataOptions options;
    options.vehicle = config.services().vehicle_data;
    options.can = config.hardware().can;
    options.source = source_override.empty() ? options.vehicle.source : source_override;
    options.samples = samples;
    options.forever = forever;
    impl_->persistent = forever;
    impl_->service = std::make_unique<VehicleDataService>(
        std::move(options),
        [this](const VehicleState& state) {
          impl_->grpc.Publish(state);
        },
        [this] {
          return !impl_->stopping.load();
        },
        [this](const can::CanLinkStatus& status) {
          impl_->grpc.PublishLinkStatus(status);
        });
    impl_->running.store(true);
    impl_->worker = std::thread([this] {
      int result = impl_->service->Run();
      if (!impl_->stopping.load() && result == 0 && impl_->persistent) {
        result = 1;
      }
      impl_->result.store(result);
      impl_->running.store(false);
    });
    return true;
  } catch (const std::exception& error) {
    LOG_ERROR("failed to configure vehicle driver: " + std::string(error.what()));
    impl_.reset();
    return false;
  }
}

void VehicleRuntime::Stop() {
  if (impl_ == nullptr) {
    return;
  }
  impl_->stopping.store(true);
  if (impl_->worker.joinable()) {
    impl_->worker.join();
  }
  impl_->grpc.Shutdown();
  impl_.reset();
}

int VehicleRuntime::Poll() const {
  if (impl_ == nullptr) {
    return 1;
  }
  if (impl_->running.load()) {
    return 0;
  }
  const int result = impl_->result.load();
  return result == 0 ? 1 : result;
}

}  // namespace vehicle
}  // namespace cockpit
