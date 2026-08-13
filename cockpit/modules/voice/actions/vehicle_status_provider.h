#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace cockpit {
namespace voice {

struct VehicleStatusSnapshot {
  std::int64_t timestamp_ms = 0;
  double speed_kph = 0.0;
  int gear = 0;
  int soc_percent = 0;
  std::string source;
};

class VehicleStatusProvider {
 public:
  virtual ~VehicleStatusProvider() = default;

  virtual bool GetLatest(std::chrono::steady_clock::time_point deadline,
                         VehicleStatusSnapshot* status, std::string* error) = 0;
  virtual void Cancel() {
  }
};

}  // namespace voice
}  // namespace cockpit
