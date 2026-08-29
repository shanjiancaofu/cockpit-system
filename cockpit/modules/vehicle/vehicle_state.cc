#include "cockpit/modules/vehicle/vehicle_state.h"

#include <iomanip>
#include <sstream>

#include "cockpit/core/json/json.h"
#include "cockpit/core/time/time.h"

namespace cockpit {
namespace vehicle {

std::string VehicleState::ToJson() const {
  std::ostringstream out;
  out << std::fixed << std::setprecision(1);
  out << "{"
      << "\"timestamp_ms\":" << timestamp_ms << ',' << "\"speed_kph\":" << speed_kph << ','
      << "\"gear\":" << gear << ',' << "\"soc_percent\":" << soc_percent << ','
      << "\"cloud_enabled\":" << (cloud_enabled ? "true" : "false") << ',' << "\"source\":\""
      << json::EscapeString(source) << "\""
      << "}";
  return out.str();
}

VehicleState MakeMockVehicleState(int sequence) {
  VehicleState state;
  state.timestamp_ms = cockpit::time::WallTime::Now().ToMilliseconds();
  state.speed_kph = 12.0 + (sequence % 9) * 3.5;
  state.gear = sequence % 4;
  state.soc_percent = 88 - (sequence % 8);
  state.cloud_enabled = true;
  state.source = "mock";
  return state;
}

}  // namespace vehicle
}  // namespace cockpit
