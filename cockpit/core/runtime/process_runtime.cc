#include "cockpit/core/runtime/process_runtime.h"

#include <atomic>
#include <csignal>
#include <string>
#include <utility>

#include "cockpit/core/logging/logger.h"

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

ProcessRuntime ProcessRuntime::Create(int argc, char** argv, const std::string& process_name) {
  InstallSignalHandlers();
  Args args = Args::Parse(argc, argv);
  const std::string config_path = args.GetString("config", "configs/config.yaml");
  auto config = config::SystemConfig::LoadFromFile(config_path);

  const std::string& log_dir = config.paths().log_dir;
  const auto log_level = logging::ParseLevel(config.logging().level);
  logging::InitLogger(process_name, log_dir, log_level, config.logging().mirror_stderr,
                      config.logging().dump_time_secs, config.logging().cut_off_time_mins,
                      config.logging().max_files);
  LOG_INFO(process_name + " started config=" + config_path);

  return ProcessRuntime(process_name, config_path, args, config);
}

ProcessRuntime::ProcessRuntime(std::string process_name, std::string config_path, Args args,
                               config::SystemConfig config)
    : process_name_(std::move(process_name)),
      config_path_(std::move(config_path)),
      args_(std::move(args)),
      config_(std::move(config)) {
}

bool ProcessRuntime::ShouldStop() const {
  return g_shutdown_requested.load();
}

void ProcessRuntime::MarkStopped() const {
  LOG_INFO(process_name_ + " stopped");
}

}  // namespace runtime
}  // namespace cockpit
