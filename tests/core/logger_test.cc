#include "cockpit/core/logging/logger.h"

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace {

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

std::vector<std::filesystem::path> LogFiles(const std::filesystem::path& root) {
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(root)) {
    const std::string name = entry.path().filename().string();
    if (entry.is_regular_file() && name.rfind("rotation_", 0) == 0 &&
        entry.path().extension() == ".log") {
      files.push_back(entry.path());
    }
  }
  return files;
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool Contains(const std::vector<std::filesystem::path>& files, const std::string& marker) {
  return std::any_of(files.begin(), files.end(), [&marker](const auto& file) {
    return ReadFile(file).find(marker) != std::string::npos;
  });
}

}  // namespace

int main() {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / ("cockpit-logger-test-" + std::to_string(getpid()));
  std::filesystem::remove_all(root);

  cockpit::logging::InitLogger("rotation", root.string(), cockpit::logging::Level::kInfo, false,
                               0.02, 0.001, 10);
  LOG_INFO("first-marker");
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  if (!Check(Contains(LogFiles(root), "first-marker"),
             "dump interval did not flush the buffered log")) {
    return 1;
  }

  for (int attempt = 0; attempt < 40 && LogFiles(root).size() < 2; ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  LOG_INFO("second-marker");
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  if (!Check(LogFiles(root).size() == 2, "time cut-off did not create a second log file") ||
      !Check(Contains(LogFiles(root), "second-marker"),
             "new log file is missing the post-cut-off message")) {
    return 1;
  }

  for (int index = 0; index < 9; ++index) {
    cockpit::logging::InitLogger("rotation", root.string(), cockpit::logging::Level::kInfo, false,
                                 0.02, 5.0, 10);
  }
  const auto retained_files = LogFiles(root);
  return Check(retained_files.size() == 10, "logger retained more than ten module log files") &&
                 Check(!Contains(retained_files, "first-marker"),
                       "logger did not delete the oldest module log file")
             ? 0
             : 1;
}
