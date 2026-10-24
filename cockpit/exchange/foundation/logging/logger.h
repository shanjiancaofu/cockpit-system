#pragma once

#include <string>

namespace cockpit {
namespace logging {

enum class Level {
  kDebug = 0,
  kInfo = 1,
  kWarn = 2,
  kError = 3,
};

void InitLogger(const std::string& service_name, const std::string& log_dir,
                Level min_level = Level::kInfo, bool mirror_stderr = true,
                double dump_time_secs = 5.0, double cut_off_time_mins = 5.0, int max_files = 10);
void Log(Level level, const char* file, int line, const std::string& message);
Level ParseLevel(const std::string& value, Level default_level = Level::kInfo);

}  // namespace logging
}  // namespace cockpit

#define LOG_DEBUG(message) \
  ::cockpit::logging::Log(::cockpit::logging::Level::kDebug, __FILE__, __LINE__, (message))
#define LOG_INFO(message) \
  ::cockpit::logging::Log(::cockpit::logging::Level::kInfo, __FILE__, __LINE__, (message))
#define LOG_WARN(message) \
  ::cockpit::logging::Log(::cockpit::logging::Level::kWarn, __FILE__, __LINE__, (message))
#define LOG_ERROR(message) \
  ::cockpit::logging::Log(::cockpit::logging::Level::kError, __FILE__, __LINE__, (message))
