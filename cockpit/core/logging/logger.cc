#include "cockpit/core/logging/logger.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace cockpit {
namespace logging {
namespace {

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

std::string FormatTime(std::chrono::system_clock::time_point now, const char* format) {
  const auto now_time = std::chrono::system_clock::to_time_t(now);
  std::tm local_time{};
  localtime_r(&now_time, &local_time);
  std::ostringstream out;
  out << std::put_time(&local_time, format);
  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
  out << '_' << std::setw(3) << std::setfill('0') << millis.count();
  return out.str();
}

class LoggerState {
 public:
  ~LoggerState() {
    Stop();
  }

  void Init(const std::string& service_name, const std::string& log_dir, Level min_level,
            bool mirror_stderr, double dump_time_secs, double cut_off_time_mins, int max_files) {
    Stop();

    std::lock_guard<std::mutex> lock(mutex_);
    service_name_ = service_name;
    log_dir_ = log_dir;
    min_level_ = min_level;
    mirror_stderr_ = mirror_stderr;
    max_files_ = max_files;
    dump_interval_ = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(dump_time_secs));
    cut_off_interval_ = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double, std::ratio<60>>(cut_off_time_mins));
    std::filesystem::create_directories(log_dir_);
    OpenFile();
    stopping_ = false;
    worker_ = std::thread([this] {
      Run();
    });
  }

  void Write(Level level, const char* file, int line, const std::string& message) {
    std::ostringstream out;
    out << FormatTime(std::chrono::system_clock::now(), "%Y-%m-%d %H:%M:%S") << " ["
        << LevelName(level) << "] " << message << " (" << file << ':' << line << ")\n";
    const std::string line_text = out.str();

    std::lock_guard<std::mutex> lock(mutex_);
    if (static_cast<int>(level) < static_cast<int>(min_level_)) {
      return;
    }
    buffer_ += line_text;
    has_entries_ = true;
    if (mirror_stderr_) {
      std::cerr << line_text;
    }
  }

 private:
  void Stop() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
      wake_.notify_all();
    }
    if (worker_.joinable()) {
      worker_.join();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    Flush();
    if (output_.is_open()) {
      output_.close();
    }
  }

  void Run() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopping_) {
      wake_.wait_until(lock, std::min(next_dump_, next_cut_off_), [this] {
        return stopping_;
      });
      if (stopping_) {
        break;
      }

      const auto now = std::chrono::steady_clock::now();
      if (now >= next_dump_) {
        Flush();
        next_dump_ = now + dump_interval_;
      }
      if (now >= next_cut_off_) {
        if (has_entries_) {
          Flush();
          output_.close();
          OpenFile();
        } else {
          next_cut_off_ = now + cut_off_interval_;
        }
      }
    }
    Flush();
  }

  void OpenFile() {
    const auto system_now = std::chrono::system_clock::now();
    const std::string prefix = service_name_ + "_";
    const std::string base_name = prefix + FormatTime(system_now, "%Y%m%d_%H%M%S");
    std::ostringstream name;
    name << base_name << '_' << std::setw(6) << std::setfill('0') << file_sequence_++ << ".log";
    std::filesystem::path path = std::filesystem::path(log_dir_) / name.str();
    while (std::filesystem::exists(path)) {
      name.str("");
      name.clear();
      name << base_name << '_' << std::setw(6) << std::setfill('0') << file_sequence_++ << ".log";
      path = std::filesystem::path(log_dir_) / name.str();
    }
    output_.open(path, std::ios::out | std::ios::trunc);
    current_path_ = path;
    buffer_ = "--- log started " + FormatTime(system_now, "%Y-%m-%d %H:%M:%S") +
              " service=" + service_name_ + " ---\n";
    has_entries_ = false;

    const auto steady_now = std::chrono::steady_clock::now();
    next_dump_ = steady_now + dump_interval_;
    next_cut_off_ = steady_now + cut_off_interval_;
    PruneOldFiles(prefix);
  }

  void Flush() {
    if (output_.is_open() && !buffer_.empty()) {
      output_ << buffer_;
      output_.flush();
      buffer_.clear();
    }
  }

  void PruneOldFiles(const std::string& prefix) const {
    using LogFile = std::pair<std::filesystem::file_time_type, std::filesystem::path>;
    std::vector<LogFile> files;
    for (const auto& entry : std::filesystem::directory_iterator(log_dir_)) {
      const std::string name = entry.path().filename().string();
      const bool timestamped = name.rfind(prefix, 0) == 0 && entry.path().extension() == ".log";
      const bool legacy =
          name == service_name_ + ".log" || name.rfind(service_name_ + ".log.", 0) == 0;
      if (entry.is_regular_file() && (timestamped || legacy) && entry.path() != current_path_) {
        std::error_code error;
        const auto modified_at = entry.last_write_time(error);
        if (!error) {
          files.emplace_back(modified_at, entry.path());
        }
      }
    }
    std::sort(files.begin(), files.end());
    while (files.size() + 1U > static_cast<std::size_t>(max_files_)) {
      std::error_code error;
      std::filesystem::remove(files.front().second, error);
      if (error) {
        break;
      }
      files.erase(files.begin());
    }
  }

  std::mutex mutex_;
  std::condition_variable wake_;
  std::thread worker_;
  std::ofstream output_;
  std::filesystem::path current_path_;
  std::string service_name_;
  std::string log_dir_;
  std::string buffer_;
  Level min_level_{Level::kInfo};
  bool mirror_stderr_{true};
  bool stopping_{true};
  bool has_entries_{false};
  int max_files_{10};
  std::uint64_t file_sequence_{0};
  std::chrono::steady_clock::duration dump_interval_{std::chrono::seconds(5)};
  std::chrono::steady_clock::duration cut_off_interval_{std::chrono::minutes(5)};
  std::chrono::steady_clock::time_point next_dump_;
  std::chrono::steady_clock::time_point next_cut_off_;
};

LoggerState& Logger() {
  static LoggerState logger;
  return logger;
}

}  // namespace

void InitLogger(const std::string& service_name, const std::string& log_dir, Level min_level,
                bool mirror_stderr, double dump_time_secs, double cut_off_time_mins,
                int max_files) {
  Logger().Init(service_name, log_dir, min_level, mirror_stderr, dump_time_secs, cut_off_time_mins,
                max_files);
}

void Log(Level level, const char* file, int line, const std::string& message) {
  Logger().Write(level, file, line, message);
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
