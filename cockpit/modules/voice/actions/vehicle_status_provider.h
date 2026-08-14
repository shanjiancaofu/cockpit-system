#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "cockpit/modules/voice/actions/action_dispatcher.h"

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

  virtual bool GetLatest(const ActionExecutionContext& context, VehicleStatusSnapshot* status,
                         std::string* error) = 0;
  virtual void Cancel() {
  }
};

}  // namespace voice
}  // namespace cockpit
