#include "tools/cockpit-ctl/snapshot_command.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cockpit/core/build/build_info.h"
#include "cockpit/core/json/json.h"
#include "cockpit/navigator/connection/ipc_connector.h"
#include "tools/cockpit-ctl/status_command.h"
#include "tools/diagnostics/cli_output.h"

namespace cockpit {
namespace ctl {
namespace snapshot {
namespace {

constexpr int kDefaultMaxLogBytes = 256 * 1024;
constexpr int kMaximumLogBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaximumLogFiles = 32;

struct IncludedLog {
  std::string name;
  std::uintmax_t bytes = 0;
  bool truncated = false;
};

void WriteTextFile(const std::filesystem::path& path, std::string_view content) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(content.data(), static_cast<std::streamsize>(content.size()));
  if (!output.good()) {
    throw std::runtime_error("write diagnostic file failed: " + path.string());
  }
}

bool IsLogFileName(const std::string& name) {
  if (!std::all_of(name.begin(), name.end(), [](unsigned char value) {
        return std::isalnum(value) != 0 || value == '.' || value == '_' || value == '-';
      })) {
    return false;
  }
  return (name.size() >= 4 && name.compare(name.size() - 4, 4, ".log") == 0) ||
         (name.size() >= 6 && name.compare(name.size() - 6, 6, ".log.1") == 0);
}

IncludedLog CopyLogTail(const std::filesystem::path& source,
                        const std::filesystem::path& destination, std::uintmax_t max_bytes) {
  const std::uintmax_t file_bytes = std::filesystem::file_size(source);
  const std::uintmax_t copied_bytes = std::min(file_bytes, max_bytes);
  std::ifstream input(source, std::ios::binary);
  if (!input.is_open()) {
    throw std::runtime_error("open log file failed: " + source.string());
  }
  input.seekg(static_cast<std::streamoff>(file_bytes - copied_bytes));
  std::string content(static_cast<std::size_t>(copied_bytes), '\0');
  input.read(content.data(), static_cast<std::streamsize>(content.size()));
  if (input.gcount() != static_cast<std::streamsize>(content.size())) {
    throw std::runtime_error("read log file failed: " + source.string());
  }
  WriteTextFile(destination, content);
  return {source.filename().string(), copied_bytes, copied_bytes < file_bytes};
}

}  // namespace

int Run(const config::SystemConfig& config, const runtime::Args& args) {
  using diagnostics::ExitCode;
  using diagnostics::ToInt;

  const int max_log_bytes = args.GetInt("max-log-bytes", kDefaultMaxLogBytes);
  if (max_log_bytes < 1 || max_log_bytes > kMaximumLogBytes) {
    std::cerr << "max-log-bytes must be between 1 and " << kMaximumLogBytes << '\n';
    return ToInt(ExitCode::kInvalidArguments);
  }

  const auto generated_at_ms =
      static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count());
  const std::filesystem::path default_directory = std::filesystem::path(config.paths().data_dir) /
                                                  "diagnostics" /
                                                  ("snapshot-" + std::to_string(generated_at_ms));
  const std::filesystem::path output_directory =
      std::filesystem::absolute(args.GetString("directory", default_directory.string()));
  const std::filesystem::path partial_directory = output_directory.string() + ".partial";

  std::error_code filesystem_error;
  if (std::filesystem::exists(
          std::filesystem::symlink_status(output_directory, filesystem_error)) ||
      std::filesystem::exists(
          std::filesystem::symlink_status(partial_directory, filesystem_error))) {
    std::cerr << "diagnostic snapshot path already exists: " << output_directory << '\n';
    return ToInt(ExitCode::kOperationFailed);
  }

  try {
    std::filesystem::create_directories(partial_directory / "logs");

    const std::string service_status = status::CaptureJson(config);
    std::string json_error;
    if (!json::IsValidValue(service_status, &json_error)) {
      throw std::runtime_error("service status JSON is invalid: " + json_error);
    }
    WriteTextFile(partial_directory / "service_status.json", service_status + "\n");

    const std::string socket_path = args.GetString(
        "socket", (std::filesystem::path(config.paths().run_dir) / "navigator.sock").string());
    std::string runtime_status;
    std::string runtime_error;
    const bool runtime_available = navigator::IpcConnector::SendRequest(
        socket_path, "status", &runtime_status, &runtime_error);
    WriteTextFile(partial_directory / "runtime_status.txt",
                  runtime_available ? runtime_status : "unavailable: " + runtime_error + "\n");

    using LogFile = std::pair<std::filesystem::file_time_type, std::filesystem::path>;
    std::vector<LogFile> log_files;
    const std::filesystem::path log_directory = config.paths().log_dir;
    const auto log_status = std::filesystem::symlink_status(log_directory, filesystem_error);
    if (!filesystem_error && std::filesystem::is_directory(log_status)) {
      for (const auto& entry : std::filesystem::directory_iterator(log_directory)) {
        const std::string name = entry.path().filename().string();
        if (IsLogFileName(name) && std::filesystem::is_regular_file(entry.symlink_status())) {
          const auto modified_at = entry.last_write_time(filesystem_error);
          if (!filesystem_error) {
            log_files.emplace_back(modified_at, entry.path());
          }
          filesystem_error.clear();
        }
      }
    }
    std::sort(log_files.begin(), log_files.end(), std::greater<>());
    const std::size_t logs_omitted =
        log_files.size() > kMaximumLogFiles ? log_files.size() - kMaximumLogFiles : 0;
    if (log_files.size() > kMaximumLogFiles) {
      log_files.resize(kMaximumLogFiles);
    }

    std::vector<IncludedLog> included_logs;
    std::size_t logs_failed = 0;
    included_logs.reserve(log_files.size());
    for (const auto& log_file : log_files) {
      const auto& log_path = log_file.second;
      const std::filesystem::path destination = partial_directory / "logs" / log_path.filename();
      try {
        included_logs.push_back(
            CopyLogTail(log_path, destination, static_cast<std::uintmax_t>(max_log_bytes)));
      } catch (const std::exception&) {
        std::error_code remove_error;
        std::filesystem::remove(destination, remove_error);
        ++logs_failed;
      }
    }

    const build::BuildInfo build_info = build::GetBuildInfo();
    std::ostringstream manifest;
    manifest
        << "{\"schema_version\":1,\"generated_at_ms\":" << generated_at_ms
        << ",\"system\":{\"name\":\"" << json::EscapeString(config.system().name)
        << "\",\"vehicle_id\":\"" << json::EscapeString(config.system().vehicle_id)
        << "\"},\"build\":{\"version\":\"" << json::EscapeString(build_info.version)
        << "\",\"build_type\":\"" << json::EscapeString(build_info.build_type)
        << "\",\"git_commit\":\"" << json::EscapeString(build_info.git_commit)
        << "\",\"git_dirty\":" << (build_info.git_dirty ? "true" : "false")
        << "},\"config_path\":\""
        << json::EscapeString(
               std::filesystem::absolute(args.GetString("config", "configs/config.yaml")).string())
        << "\",\"runtime\":{\"available\":" << (runtime_available ? "true" : "false")
        << ",\"socket\":\"" << json::EscapeString(socket_path) << "\",\"error\":\""
        << json::EscapeString(runtime_error) << "\"},\"max_log_bytes\":" << max_log_bytes
        << ",\"logs_omitted\":" << logs_omitted << ",\"logs_failed\":" << logs_failed
        << ",\"logs\":[";
    for (std::size_t index = 0; index < included_logs.size(); ++index) {
      if (index != 0) {
        manifest << ',';
      }
      const IncludedLog& log = included_logs[index];
      manifest << "{\"name\":\"" << json::EscapeString(log.name) << "\",\"bytes\":" << log.bytes
               << ",\"truncated\":" << (log.truncated ? "true" : "false") << '}';
    }
    manifest << "]}\n";
    const std::string manifest_text = manifest.str();
    if (!json::IsValidValue(manifest_text, &json_error)) {
      throw std::runtime_error("diagnostic manifest JSON is invalid: " + json_error);
    }
    WriteTextFile(partial_directory / "manifest.json", manifest_text);

    std::filesystem::create_directories(output_directory.parent_path());
    std::filesystem::rename(partial_directory, output_directory);
    std::cout << "diagnostic snapshot: " << output_directory.string() << '\n';
    return ToInt(ExitCode::kSuccess);
  } catch (const std::exception& exception) {
    std::filesystem::remove_all(partial_directory, filesystem_error);
    std::cerr << exception.what() << '\n';
    return ToInt(ExitCode::kOperationFailed);
  }
}

}  // namespace snapshot
}  // namespace ctl
}  // namespace cockpit
