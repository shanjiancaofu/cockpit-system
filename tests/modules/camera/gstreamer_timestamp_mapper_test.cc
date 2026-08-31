#include "cockpit/modules/camera/capture/gstreamer_timestamp_mapper.h"

#include <cstdint>
#include <iostream>

namespace {

bool Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  std::int64_t mapped = 0;
  if (!Require(cockpit::camera::MapGstreamerRunningTimeToRealtime(900, 1000, 1000000, 1000020,
                                                                  &mapped) &&
                   mapped == 999910,
               "running time must map from a current clock snapshot")) {
    return 1;
  }

  std::int64_t next = 0;
  if (!Require(
          cockpit::camera::MapGstreamerRunningTimeToRealtime(933, 1033, 1000033, 1000053, &next) &&
              next - mapped == 33,
          "mapped source timestamps must preserve source progression")) {
    return 1;
  }

  std::int64_t delayed = 0;
  if (!Require(cockpit::camera::MapGstreamerRunningTimeToRealtime(900, 1500, 1000500, 1000520,
                                                                  &delayed) &&
                   delayed == mapped,
               "callback latency must not bias the mapped source timestamp")) {
    return 1;
  }

  if (!Require(!cockpit::camera::MapGstreamerRunningTimeToRealtime(1001, 1000, 1000000, 1000020,
                                                                   &mapped) &&
                   !cockpit::camera::MapGstreamerRunningTimeToRealtime(900, 1000, 1000020, 1000000,
                                                                       &mapped) &&
                   !cockpit::camera::MapGstreamerRunningTimeToRealtime(900, 1000, 1000000, 1000020,
                                                                       nullptr),
               "invalid clock snapshots must fail closed")) {
    return 1;
  }

  std::cout << "GStreamer timestamp mapper tests passed\n";
  return 0;
}
