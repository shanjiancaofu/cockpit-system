#pragma once

#include <memory>
#include <string>

namespace cockpit {
namespace vehicle {

class VehicleRuntime {
 public:
  VehicleRuntime();
  ~VehicleRuntime();

  VehicleRuntime(const VehicleRuntime&) = delete;
  VehicleRuntime& operator=(const VehicleRuntime&) = delete;

  bool Start(const std::string& config_path, const std::string& source_override = "",
             int samples = 5, bool forever = true);
  void Stop();
  int Poll() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace vehicle
}  // namespace cockpit
