#include "cockpit/navigator/diagnostics/crash_reporter.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

#include "cockpit/core/json/json.h"

namespace cockpit {
namespace navigator {
namespace {

bool IsCrashReport(const std::filesystem::path& path) {
  const std::string name = path.filename().string();
  return name.rfind("crash-", 0) == 0 && path.extension() == ".json";
}

}  // namespace

CrashReporter::CrashReporter(std::filesystem::path directory, std::size_t max_reports)
    : directory_(std::move(directory)), max_reports_(std::max<std::size_t>(1, max_reports)) {
}

bool CrashReporter::Record(const CrashReport& report, std::string* error) const {
  if (report.timestamp_ms <= 0 || report.module.empty() || report.pid <= 0 ||
      report.termination.empty() || report.restart_result.empty()) {
    *error = "invalid crash report";
    return false;
  }

  std::error_code filesystem_error;
  std::filesystem::create_directories(directory_, filesystem_error);
  if (filesystem_error) {
    *error = "failed to create crash report directory: " + filesystem_error.message();
    return false;
  }

  const std::string filename =
      "crash-" + std::to_string(report.timestamp_ms) + "-" + std::to_string(report.pid) + ".json";
  const std::filesystem::path final_path = directory_ / filename;
  const std::filesystem::path temporary_path = directory_ / (filename + ".tmp");

  std::ofstream output(temporary_path, std::ios::trunc);
  if (!output.is_open()) {
    *error = "failed to open crash report: " + temporary_path.string();
    return false;
  }
  output << "{"
         << "\"schema_version\":1,"
         << "\"timestamp_ms\":" << report.timestamp_ms << ',' << "\"module\":\""
         << json::EscapeString(report.module) << "\","
         << "\"mode\":\"" << json::EscapeString(report.mode) << "\","
         << "\"pid\":" << report.pid << ',' << "\"termination\":\""
         << json::EscapeString(report.termination) << "\","
         << "\"exit_code\":" << report.exit_code << ',' << "\"signal\":" << report.signal << ','
         << "\"core_dumped\":" << (report.core_dumped ? "true" : "false") << ','
         << "\"restart_result\":\"" << json::EscapeString(report.restart_result) << "\","
         << "\"restart_count\":" << report.restart_count << ','
         << "\"replacement_pid\":" << report.replacement_pid << "}\n";
  output.close();
  if (!output) {
    std::filesystem::remove(temporary_path, filesystem_error);
    *error = "failed to write crash report: " + temporary_path.string();
    return false;
  }

  std::filesystem::rename(temporary_path, final_path, filesystem_error);
  if (filesystem_error) {
    std::error_code remove_error;
    std::filesystem::remove(temporary_path, remove_error);
    *error = "failed to publish crash report: " + filesystem_error.message();
    return false;
  }
  return Prune(error);
}

bool CrashReporter::Prune(std::string* error) const {
  std::error_code filesystem_error;
  std::vector<std::filesystem::path> reports;
  for (std::filesystem::directory_iterator entry(directory_, filesystem_error), end;
       entry != end && !filesystem_error; entry.increment(filesystem_error)) {
    std::error_code type_error;
    if (entry->is_regular_file(type_error) && !type_error && IsCrashReport(entry->path())) {
      reports.push_back(entry->path());
    }
  }
  if (filesystem_error) {
    *error = "failed to enumerate crash reports: " + filesystem_error.message();
    return false;
  }

  std::sort(reports.begin(), reports.end());
  while (reports.size() > max_reports_) {
    std::filesystem::remove(reports.front(), filesystem_error);
    if (filesystem_error) {
      *error = "failed to prune crash report: " + filesystem_error.message();
      return false;
    }
    reports.erase(reports.begin());
  }
  return true;
}

}  // namespace navigator
}  // namespace cockpit
