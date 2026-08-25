#include "cockpit/modules/sentinel/sentinel_service.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "cockpit/core/time/time.h"

namespace {

using cockpit::sentinel::SentinelActions;
using cockpit::sentinel::SentinelService;
using cockpit::sentinel::SentinelSnapshot;
using cockpit::sentinel::SentinelState;
using cockpit::vehicle::ChassisEvent;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

class FakeActions final : public SentinelActions {
 public:
  bool PrepareRecording(std::uint64_t, std::string*) override {
    ++prepares;
    return true;
  }
  bool TakeSnapshot(std::uint64_t sequence, SentinelSnapshot* snapshot,
                    std::string* error) override {
    ++photos;
    if (!photo_success.load()) {
      *error = "photo failed";
      return false;
    }
    snapshot->path = "/tmp/sentinel-" + std::to_string(sequence) + ".jpg";
    snapshot->size_bytes = 42;
    return true;
  }

  bool RecordMotion(const ChassisEvent&, const SentinelSnapshot&, std::string* error) override {
    ++recordings;
    if (!record_success.load()) {
      *error = "record failed";
      return false;
    }
    return true;
  }

  void Cancel() override {
    ++cancels;
  }

  std::atomic_int prepares{0};
  std::atomic_int photos{0};
  std::atomic_int recordings{0};
  std::atomic_int cancels{0};
  std::atomic_bool photo_success{true};
  std::atomic_bool record_success{true};
};

ChassisEvent Motion(std::uint64_t sequence, std::int64_t timestamp_ms = 0) {
  ChassisEvent event;
  event.sequence = sequence;
  event.timestamp_ms = timestamp_ms == 0 ? cockpit::time::NowMs() : timestamp_ms;
  event.source = "fake-stm32";
  event.sensor_id = 1;
  event.motion_detected = true;
  return event;
}

bool WaitFor(const std::function<bool()>& predicate,
             std::chrono::milliseconds timeout = std::chrono::seconds(1)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return predicate();
}

}  // namespace

int main() {
  auto actions = std::make_unique<FakeActions>();
  FakeActions* fake = actions.get();
  cockpit::sentinel::SentinelPolicy policy;
  policy.cooldown = std::chrono::milliseconds(40);
  policy.max_event_age = std::chrono::milliseconds(100);
  policy.queue_capacity = 2;
  SentinelService service(policy, std::move(actions));
  Require(service.Start(false), "start failed");
  Require(!service.Submit(Motion(1)), "disabled event must be rejected");

  std::string error;
  Require(service.Arm(&error), "arm failed");
  Require(service.Submit(Motion(1)), "armed event rejected");
  Require(WaitFor([&] {
            return service.status().accepted_events == 1;
          }),
          "motion did not complete");
  Require(fake->prepares == 1 && fake->photos == 1 && fake->recordings == 1,
          "actions were not executed once");
  Require(service.status().state == SentinelState::kCooldown, "cooldown not entered");
  const auto cooldown_until_ms = service.status().cooldown_until_ms;
  Require(service.Arm(&error), "idempotent arm during cooldown failed");
  Require(service.status().state == SentinelState::kCooldown &&
              service.status().cooldown_until_ms == cooldown_until_ms,
          "arm bypassed the active cooldown");

  Require(service.Submit(Motion(1)), "duplicate was not accepted into bounded queue");
  Require(service.Submit(Motion(2)), "cooldown event was not accepted into bounded queue");
  Require(WaitFor([&] {
            return service.status().suppressed_events == 2;
          }),
          "duplicate/cooldown events not suppressed");
  Require(fake->photos == 1, "suppressed event reached actions");
  Require(WaitFor([&] {
            return service.status().state == SentinelState::kArmed;
          }),
          "cooldown did not return to armed");

  Require(service.Submit(Motion(3, cockpit::time::NowMs() - 1000)), "stale event rejected early");
  Require(WaitFor([&] {
            return service.status().suppressed_events == 3;
          }),
          "stale event not suppressed");

  fake->photo_success.store(false);
  Require(service.Submit(Motion(4)), "failure event rejected");
  Require(WaitFor([&] {
            return service.status().failed_events == 1;
          }),
          "photo failure not recorded");
  Require(service.status().last_error == "photo failed", "photo failure detail missing");
  Require(fake->recordings == 1, "record action ran after photo failure");

  Require(service.Disarm(&error), "disarm failed");
  Require(service.status().state == SentinelState::kDisabled, "disarm state incorrect");
  service.Stop();
  Require(fake->cancels >= 2, "disarm/stop did not cancel actions");
  std::cout << "sentinel service tests passed\n";
  return 0;
}
