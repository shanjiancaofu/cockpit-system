#pragma once

#include <string>

#include "cockpit/modules/vehicle/chassis_safety_adapter.h"

namespace cockpit::vehicle {

class ChassisCommandSink {
 public:
  virtual ~ChassisCommandSink() = default;
  virtual bool Send(const SafeChassisCommand& command, std::string* error) = 0;
};

}  // namespace cockpit::vehicle
