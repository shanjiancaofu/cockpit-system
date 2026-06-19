#include "common/runtime/ServiceRuntime.h"

#include "common/logging/Logger.h"

#include <atomic>
#include <csignal>
#include <string>
#include <utility>

namespace cockpit {
namespace runtime {
namespace {

std::atomic_bool g_shutdown_requested{false};

void OnSignal(int) {
  g_shutdown_requested.store(true);
}

void InstallSignalHandlers() {
  std::signal(SIGINT, OnSignal);
  std::signal(SIGTERM, OnSignal);
}

}  // namespace

ServiceRuntime ServiceRuntime::Create(int argc, char** argv, const std::string& service_name) {
  InstallSignalHandlers();
  Args args = Args::Parse(argc, argv);
  const std::string config_path = args.GetString("config", "configs/config.yaml");
  auto config = config::Config::LoadFromFile(config_path);

  const std::string log_dir = config.GetString("logging.dir", "logs");
  const int max_bytes = config.GetInt("logging.max_bytes", 2 * 1024 * 1024);
  const auto log_level = logging::ParseLevel(config.GetString("logging.level", "info"));
  logging::InitLogger(service_name, log_dir, log_level, max_bytes);
  LOG_INFO(service_name + " started config=" + config_path);

  return ServiceRuntime(service_name, config_path, args, config);
}

ServiceRuntime::ServiceRuntime(std::string service_name,
                               std::string config_path,
                               Args args,
                               config::Config config)
    : service_name_(std::move(service_name)),
      config_path_(std::move(config_path)),
      args_(std::move(args)),
      config_(std::move(config)) {}

bool ServiceRuntime::ShouldStop() const {
  return g_shutdown_requested.load();
}

void ServiceRuntime::MarkStopped() const {
  LOG_INFO(service_name_ + " stopped");
}

}  // namespace runtime
}  // namespace cockpit
