#pragma once

#include <memory>
#include <string>

namespace cockpit {
namespace recording {

class RecordingRuntime {
 public:
  RecordingRuntime();
  ~RecordingRuntime();

  RecordingRuntime(const RecordingRuntime&) = delete;
  RecordingRuntime& operator=(const RecordingRuntime&) = delete;

  bool Start(const std::string& config_path, const std::string& directory_override = "");
  void Stop();
  int Poll() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace recording
}  // namespace cockpit
