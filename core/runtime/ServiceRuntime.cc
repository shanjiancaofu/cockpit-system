#include "core/runtime/ServiceRuntime.h"

#include <atomic>
#include <csignal>
#include <string>
#include <utility>

#include "core/logging/Logger.h"

namespace cockpit {
namespace runtime {
namespace {

std::atomic_bool g_shutdown_requested{false};

void OnSignal(int) {
  g_shutdown_requested.store(true);
}

void InstallSignalHandlers() {
  if (std::signal(SIGINT, OnSignal) == SIG_ERR) {
    LOG_WARN("failed to install SIGINT handler");
  }
  if (std::signal(SIGTERM, OnSignal) == SIG_ERR) {
    LOG_WARN("failed to install SIGTERM handler");
  }
}

}  // namespace

ServiceRuntime ServiceRuntime::Create(int argc, char** argv, const std::string& service_name) {
  InstallSignalHandlers();
  Args args = Args::Parse(argc, argv);
  const std::string config_path = args.GetString("config", "configs/config.yaml");
  auto config = config::SystemConfig::LoadFromFile(config_path);

  const std::string& log_dir = config.paths().log_dir;
  const int max_bytes = config.logging().max_bytes;
  const auto log_level = logging::ParseLevel(config.logging().level);
  logging::InitLogger(service_name, log_dir, log_level, max_bytes, config.logging().mirror_stderr);
  LOG_INFO(service_name + " started config=" + config_path);

  return ServiceRuntime(service_name, config_path, args, config);
}

ServiceRuntime::ServiceRuntime(std::string service_name, std::string config_path, Args args,
                               config::SystemConfig config)
    : service_name_(std::move(service_name)),
      config_path_(std::move(config_path)),
      args_(std::move(args)),
      config_(std::move(config)) {
}

bool ServiceRuntime::ShouldStop() const {
  return g_shutdown_requested.load();
}

void ServiceRuntime::MarkStopped() const {
  LOG_INFO(service_name_ + " stopped");
}

}  // namespace runtime
}  // namespace cockpit
