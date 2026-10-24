#pragma once

#include <atomic>
#include <string>

namespace cockpit {
namespace carupload {

class CaruploadRuntime {
 public:
  bool Start(const std::string& config_path);
  void Stop();
  int Poll() const;

 private:
  std::atomic_bool running_{false};
};

}  // namespace carupload
}  // namespace cockpit
