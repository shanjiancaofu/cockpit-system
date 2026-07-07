#include "cockpit/core/event/message_bus.h"

#include <iostream>

namespace {

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  cockpit::event::MessageBus bus(2);
  auto vehicle = bus.Subscribe("/vehicle/state");
  auto all = bus.Subscribe("*");
  auto small = bus.Subscribe("/vehicle/state", 1);

  cockpit::event::EventMessage first;
  first.topic = "/vehicle/state";
  first.type = "VehicleState";
  first.source = "test";
  first.payload_json = "{\"speed\":12}";

  cockpit::event::EventMessage second = first;
  second.payload_json = "{\"speed\":13}";

  cockpit::event::EventMessage camera;
  camera.topic = "/camera/frame";
  camera.type = "CameraFrameMeta";
  camera.source = "test";
  camera.payload_json = "{\"sequence\":7}";

  const bool published = bus.Publish(first) && bus.Publish(second) && bus.Publish(camera);
  const auto vehicle_first = vehicle->TryPop();
  const auto vehicle_second = vehicle->TryPop();
  const auto vehicle_empty = vehicle->TryPop();
  const auto all_first = all->TryPop();
  const auto all_second = all->TryPop();
  const auto small_first = small->TryPop();
  const auto metrics = bus.metrics();

  const bool result =
      Check(published, "publish failed") &&
      Check(vehicle_first.has_value() && vehicle_first->sequence == 1, "first vehicle missing") &&
      Check(vehicle_second.has_value() && vehicle_second->sequence == 2,
            "second vehicle missing") &&
      Check(!vehicle_empty.has_value(), "vehicle subscriber received wrong topic") &&
      Check(all_first.has_value() && all_second.has_value(),
            "wildcard subscriber missing events") &&
      Check(small_first.has_value(), "small subscriber first event missing") &&
      Check(small->DropCount() == 1, "small subscriber drop count mismatch") &&
      Check(metrics.published == 3, "published metric mismatch") &&
      Check(metrics.dropped == 2, "dropped metric mismatch");
  return result ? 0 : 1;
}
