#pragma once

#include <sys/ioctl.h>
#include <unistd.h>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace cockpit::camera_diag {

// JSON fragments are assembled only through these escaping/typed helpers.
using Json = std::string;
Json Quote(const std::string& value);
Json Object(std::initializer_list<std::pair<std::string, Json>> fields);
Json Array(const std::vector<Json>& values);
inline Json Boolean(bool value) {
  return value ? "true" : "false";
}
template <class T>
Json Number(T value) {
  return std::to_string(value);
}
Json Strings(const std::vector<std::string>& values);
bool WriteReport(const std::string& path, const Json& report);
std::string ReadText(const std::string& path);
std::vector<std::string> DevicePaths(const std::string& prefix);

class Fd {
 public:
  explicit Fd(int fd) : value(fd) {
  }
  ~Fd() {
    if (value >= 0) close(value);
  }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  int value;
};
int Ioctl(int fd, unsigned long request, void* value);
struct CommandResult {
  int code = -1;
  std::string output;
  bool truncated = false;
};
CommandResult RunCommand(const std::vector<std::string>& args, int timeout_ms = 5000);

Json InspectDevices(std::vector<std::string>* errors);
Json InspectMedia(std::vector<std::string>* errors);
Json InspectSensor(const std::string& device, std::vector<std::string>* errors);
Json InspectControls(const std::string& device, std::vector<std::string>* errors);
// Reserved for a future opt-in test pattern workflow. Never called during diagnosis.
bool SetSensorTestPattern(const std::string& device, int value, std::string* error);
Json InspectEnvironment();

struct Options {
  std::string device = "/dev/video0";
  std::string output = "camera_diagnostics.json";
  int width = 1920, height = 1080, fps = 30;
  int frames = 300, cycles = 1, timeout_ms = 5000;
  bool inspect_only = false, help = false;
};
bool ParseOptions(int argc, char** argv, Options* options, std::string* error);
Json CaptureStress(const Options& options, bool* passed, std::vector<std::string>* errors);
int RunDiagnostics(int argc, char** argv, bool stress_only);

// One instance per stream run: sequence reset at Start is expected.
struct FrameStats {
  std::uint64_t frames = 0, sequence_gaps = 0, duplicates = 0, regressions = 0;
  std::uint64_t buffer_errors = 0, timestamp_errors = 0;
  std::uint32_t previous_sequence = 0;
  std::int64_t first_timestamp = 0, previous_timestamp = 0;
  void Observe(std::uint32_t sequence, std::int64_t timestamp, bool buffer_error) {
    if (frames != 0) {
      const std::uint32_t delta = sequence - previous_sequence;
      if (delta == 0)
        ++duplicates;
      else if (delta < 0x80000000U)
        sequence_gaps += delta - 1;
      else
        ++regressions;
      if (timestamp <= previous_timestamp) ++timestamp_errors;
    } else {
      first_timestamp = timestamp;
    }
    if (timestamp <= 0) ++timestamp_errors;
    previous_sequence = sequence;
    previous_timestamp = timestamp;
    buffer_errors += buffer_error ? 1 : 0;
    ++frames;
  }
  double fps() const {
    return frames > 1 && previous_timestamp > first_timestamp
               ? static_cast<double>(frames - 1) * 1e9 / (previous_timestamp - first_timestamp)
               : 0.0;
  }
  bool healthy() const {
    return sequence_gaps == 0 && duplicates == 0 && regressions == 0 && buffer_errors == 0 &&
           timestamp_errors == 0;
  }
};
}  // namespace cockpit::camera_diag
