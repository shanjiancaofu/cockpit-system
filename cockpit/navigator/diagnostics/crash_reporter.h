#pragma once

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace cockpit {
namespace navigator {

struct CrashReport {
  std::int64_t timestamp_ms{0};
  std::string module;
  std::string mode;
  pid_t pid{0};
  std::string termination;
  int exit_code{0};
  int signal{0};
  bool core_dumped{false};
  std::string restart_result;
  int restart_count{0};
  pid_t replacement_pid{0};
};

class CrashReporter {
 public:
  explicit CrashReporter(std::filesystem::path directory, std::size_t max_reports = 20);

  bool Record(const CrashReport& report, std::string* error) const;

 private:
  bool Prune(std::string* error) const;

  std::filesystem::path directory_;
  std::size_t max_reports_;
};

}  // namespace navigator
}  // namespace cockpit
