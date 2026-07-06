#include "cockpit/core/logging/Logger.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <system_error>

namespace cockpit {
namespace logging {
namespace {

std::mutex g_mutex;
std::string g_log_path;
Level g_min_level = Level::kInfo;
bool g_mirror_stderr = true;

std::ofstream& Output() {
  static auto output = std::make_unique<std::ofstream>();
  return *output;
}

std::string LevelName(Level level) {
  switch (level) {
    case Level::kDebug:
      return "DEBUG";
    case Level::kInfo:
      return "INFO";
    case Level::kWarn:
      return "WARN";
    case Level::kError:
      return "ERROR";
  }
  return "LOG";
}

std::string NowString() {
  const auto now = std::chrono::system_clock::now();
  const auto now_time = std::chrono::system_clock::to_time_t(now);
  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
  std::tm local_time{};
#if defined(_WIN32)
  localtime_s(&local_time, &now_time);
#else
  localtime_r(&now_time, &local_time);
#endif
  std::ostringstream out;
  out << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0')
      << millis.count();
  return out.str();
}

void RotateIfNeeded(const std::string& path, long long max_bytes) {
  namespace fs = std::filesystem;
  const fs::path log_path(path);
  if (!fs::exists(log_path) || static_cast<long long>(fs::file_size(log_path)) < max_bytes) {
    return;
  }
  const fs::path rotated_path(path + ".1");
  std::error_code ec;
  fs::remove(rotated_path, ec);
  ec.clear();
  fs::rename(log_path, rotated_path, ec);
}

}  // namespace

void InitLogger(const std::string& service_name, const std::string& log_dir, Level min_level,
                long long max_bytes, bool mirror_stderr) {
  std::lock_guard<std::mutex> lock(g_mutex);
  namespace fs = std::filesystem;
  fs::create_directories(log_dir);
  g_log_path = (fs::path(log_dir) / (service_name + ".log")).string();
  RotateIfNeeded(g_log_path, max_bytes);
  auto& output = Output();
  if (output.is_open()) {
    output.close();
  }
  output.open(g_log_path, std::ios::app);
  g_min_level = min_level;
  g_mirror_stderr = mirror_stderr;
  if (output.is_open()) {
    output << "\n--- log started " << NowString() << " service=" << service_name << " ---\n";
    output.flush();
  }
}

void Log(Level level, const char* file, int line, const std::string& message) {
  if (static_cast<int>(level) < static_cast<int>(g_min_level)) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  std::ostringstream out;
  out << NowString() << " [" << LevelName(level) << "] " << message << " (" << file << ':' << line
      << ')';
  const std::string line_text = out.str();
  auto& output = Output();
  if (output.is_open()) {
    output << line_text << '\n';
    output.flush();
  }
  if (g_mirror_stderr) {
    std::cerr << line_text << '\n';
  }
}

Level ParseLevel(const std::string& value, Level default_level) {
  std::string normalized = value;
  std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  if (normalized == "debug") {
    return Level::kDebug;
  }
  if (normalized == "info") {
    return Level::kInfo;
  }
  if (normalized == "warn" || normalized == "warning") {
    return Level::kWarn;
  }
  if (normalized == "error") {
    return Level::kError;
  }
  return default_level;
}

}  // namespace logging
}  // namespace cockpit
