#pragma once

#include <sys/types.h>

#include <string>

namespace cockpit {
namespace hmi {

class HmiRuntime {
 public:
  HmiRuntime() = default;
  ~HmiRuntime();

  HmiRuntime(const HmiRuntime&) = delete;
  HmiRuntime& operator=(const HmiRuntime&) = delete;

  bool Start(const std::string& config_path);
  void Stop();
  int Poll();

 private:
  pid_t pid_{0};
  int result_{0};
};

}  // namespace hmi
}  // namespace cockpit
