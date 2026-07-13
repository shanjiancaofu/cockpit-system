#pragma once

#include <memory>
#include <string>

namespace cockpit {
namespace audio {

class AudioRuntime {
 public:
  AudioRuntime();
  ~AudioRuntime();

  AudioRuntime(const AudioRuntime&) = delete;
  AudioRuntime& operator=(const AudioRuntime&) = delete;

  bool Start(const std::string& config_path, const std::string& output_device_override = "");
  void Stop();
  int Poll() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace audio
}  // namespace cockpit
