#include "cockpit/navigator/diagnostics/crash_reporter.h"

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "cockpit/core/json/json.h"

namespace {

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return false;
  }
  return true;
}

}  // namespace

int main() {
  const std::filesystem::path test_directory =
      std::filesystem::temp_directory_path() /
      ("cockpit-crash-reporter-test-" + std::to_string(getpid()));
  cockpit::navigator::CrashReporter reporter(test_directory, 2);
  bool success = true;

  for (int index = 0; index < 3; ++index) {
    cockpit::navigator::CrashReport report;
    report.timestamp_ms = 1700000000000LL + index;
    report.module = index == 2 ? "camera_driver" : "audio_driver";
    report.mode = "normal";
    report.pid = 100 + index;
    report.termination = index == 2 ? "signal" : "exit";
    report.exit_code = index == 2 ? 139 : 1;
    report.signal = index == 2 ? 11 : 0;
    report.core_dumped = index == 2;
    report.restart_result = index == 2 ? "limit_exceeded" : "succeeded";
    report.restart_count = index + 1;
    report.replacement_pid = index == 2 ? 0 : 200 + index;
    std::string error;
    success &= Check(reporter.Record(report, &error), error.c_str());
  }

  std::size_t report_count = 0;
  bool found_latest = false;
  for (const auto& entry : std::filesystem::directory_iterator(test_directory)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".json") {
      continue;
    }
    ++report_count;
    std::ifstream input(entry.path());
    const std::string contents((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    success &= Check(cockpit::json::IsValidValue(contents), "stored crash report is invalid JSON");
    found_latest |= contents.find("\"module\":\"camera_driver\"") != std::string::npos &&
                    contents.find("\"signal\":11") != std::string::npos &&
                    contents.find("\"core_dumped\":true") != std::string::npos &&
                    contents.find("\"restart_result\":\"limit_exceeded\"") != std::string::npos;
  }

  success &= Check(report_count == 2, "crash report retention limit was not enforced");
  success &= Check(found_latest, "latest crash report fields are incomplete");
  success &= Check(!std::filesystem::exists(test_directory / "crash-1700000000000-100.json"),
                   "oldest crash report was not removed");

  std::filesystem::remove_all(test_directory);
  return success ? 0 : 1;
}
