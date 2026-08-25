#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "cockpit/core/base/macros.h"
#include "cockpit/modules/vehicle/chassis_event.h"

namespace cockpit {
namespace sentinel {

enum class SentinelState { kDisabled, kArmed, kTriggered, kCooldown, kFaulted };

struct SentinelPolicy {
  std::chrono::milliseconds cooldown{30000};
  std::chrono::milliseconds max_event_age{5000};
  std::size_t queue_capacity = 64;
};

struct SentinelSnapshot {
  std::string path;
  std::uint64_t size_bytes = 0;
};

struct SentinelStatus {
  SentinelState state = SentinelState::kDisabled;
  std::int64_t cooldown_until_ms = 0;
  std::uint64_t last_event_sequence = 0;
  std::int64_t last_event_timestamp_ms = 0;
  std::string last_snapshot_path;
  std::uint64_t accepted_events = 0;
  std::uint64_t suppressed_events = 0;
  std::uint64_t failed_events = 0;
  std::uint64_t dropped_events = 0;
  std::string last_error;
};

class SentinelActions {
 public:
  virtual ~SentinelActions() = default;
  virtual bool PrepareRecording(std::uint64_t event_sequence, std::string* error) = 0;
  virtual bool TakeSnapshot(std::uint64_t event_sequence, SentinelSnapshot* snapshot,
                            std::string* error) = 0;
  virtual bool RecordMotion(const vehicle::ChassisEvent& event, const SentinelSnapshot& snapshot,
                            std::string* error) = 0;
  virtual void Cancel() = 0;
};

class SentinelService final {
 public:
  SentinelService(SentinelPolicy policy, std::unique_ptr<SentinelActions> actions);
  ~SentinelService();
  COCKPIT_DISALLOW_COPY_AND_ASSIGN(SentinelService);

  bool Start(bool armed);
  void Stop();
  bool Arm(std::string* error);
  bool Disarm(std::string* error);
  bool Submit(vehicle::ChassisEvent event);
  SentinelStatus status() const;

 private:
  void Run();
  void UpdateCooldownLocked(std::int64_t now_ms);

  const SentinelPolicy policy_;
  std::unique_ptr<SentinelActions> actions_;
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  std::deque<vehicle::ChassisEvent> events_;
  SentinelStatus status_;
  bool running_ = false;
  bool stopping_ = false;
  std::thread worker_;
};

const char* SentinelStateName(SentinelState state);

}  // namespace sentinel
}  // namespace cockpit
