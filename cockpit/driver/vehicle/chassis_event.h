#pragma once

#include <cstdint>
#include <string>

namespace cockpit {
namespace vehicle {

enum class ChassisEventType {
  kMotionDetected,
};

struct ChassisEvent {
  std::uint64_t sequence = 0;
  std::int64_t timestamp_ms = 0;
  ChassisEventType type = ChassisEventType::kMotionDetected;
  std::string source;
  std::uint32_t sensor_id = 0;
  bool motion_detected = false;
};

}  // namespace vehicle
}  // namespace cockpit
