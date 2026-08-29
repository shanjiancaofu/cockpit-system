#include "cockpit/modules/sentinel/sentinel_service.h"

#include <algorithm>
#include <utility>

#include "cockpit/core/time/time.h"

namespace cockpit {
namespace sentinel {

const char* SentinelStateName(SentinelState state) {
  switch (state) {
    case SentinelState::kDisabled:
      return "DISABLED";
    case SentinelState::kArmed:
      return "ARMED";
    case SentinelState::kTriggered:
      return "TRIGGERED";
    case SentinelState::kCooldown:
      return "COOLDOWN";
    case SentinelState::kFaulted:
      return "FAULTED";
  }
  return "UNKNOWN";
}

SentinelService::SentinelService(SentinelPolicy policy, std::unique_ptr<SentinelActions> actions)
    : policy_(policy), actions_(std::move(actions)), events_(policy_.queue_capacity) {
}

SentinelService::~SentinelService() {
  Stop();
}

bool SentinelService::Start(bool armed) {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_ || actions_ == nullptr || policy_.cooldown.count() <= 0 ||
        policy_.max_event_age.count() <= 0 || policy_.queue_capacity == 0) {
      return false;
    }
  }
  events_.Reset();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = false;
    running_ = true;
    status_ = SentinelStatus{};
    cooldown_until_steady_ms_ = 0;
    status_.state = armed ? SentinelState::kArmed : SentinelState::kDisabled;
  }
  worker_ = std::thread(&SentinelService::Run, this);
  return true;
}

void SentinelService::Stop() {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) return;
    stopping_ = true;
  }
  events_.Close();
  static_cast<void>(events_.DiscardPending());
  actions_->Cancel();
  if (worker_.joinable()) worker_.join();
  std::lock_guard<std::mutex> lock(mutex_);
  running_ = false;
  status_.state = SentinelState::kDisabled;
  status_.cooldown_until_ms = 0;
  cooldown_until_steady_ms_ = 0;
}

bool SentinelService::Arm(std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (error == nullptr) return false;
  error->clear();
  if (!running_ || stopping_) {
    *error = "sentinel service is not running";
    return false;
  }
  if (status_.state == SentinelState::kTriggered) {
    *error = "sentinel trigger is in progress";
    return false;
  }
  if (status_.state == SentinelState::kArmed || status_.state == SentinelState::kCooldown) {
    return true;
  }
  status_.state = SentinelState::kArmed;
  status_.cooldown_until_ms = 0;
  cooldown_until_steady_ms_ = 0;
  status_.last_error.clear();
  return true;
}

bool SentinelService::Disarm(std::string* error) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (error == nullptr) return false;
    error->clear();
    if (!running_ || stopping_) {
      *error = "sentinel service is not running";
      return false;
    }
    status_.state = SentinelState::kDisabled;
    status_.cooldown_until_ms = 0;
    cooldown_until_steady_ms_ = 0;
  }
  static_cast<void>(events_.DiscardPending());
  actions_->Cancel();
  return true;
}

bool SentinelService::Submit(vehicle::ChassisEvent event) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || stopping_ || status_.state == SentinelState::kDisabled ||
        event.type != vehicle::ChassisEventType::kMotionDetected || !event.motion_detected ||
        event.sequence == 0 || event.timestamp_ms <= 0) {
      return false;
    }
  }
  const event::EventQueuePushResult result = events_.Push(std::move(event));
  if (result != event::EventQueuePushResult::kAccepted) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++status_.dropped_events;
    status_.last_error = result == event::EventQueuePushResult::kFull
                             ? "sentinel event queue is full"
                             : "sentinel event queue is closed";
    return false;
  }
  return true;
}

SentinelStatus SentinelService::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return status_;
}

void SentinelService::UpdateCooldownLocked(std::int64_t steady_now_ms) {
  if (status_.state == SentinelState::kCooldown && steady_now_ms >= cooldown_until_steady_ms_) {
    status_.state = SentinelState::kArmed;
    status_.cooldown_until_ms = 0;
    cooldown_until_steady_ms_ = 0;
  }
}

void SentinelService::Run() {
  while (true) {
    std::chrono::milliseconds wait_duration{100};
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) return;
      if (status_.state == SentinelState::kCooldown) {
        wait_duration = std::chrono::milliseconds(std::max<std::int64_t>(
            1, cooldown_until_steady_ms_ - time::SteadyTime::Now().ToMilliseconds()));
      }
    }

    std::optional<vehicle::ChassisEvent> pending = events_.WaitPopFor(wait_duration);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) return;
      const std::int64_t wall_now_ms = time::WallTime::Now().ToMilliseconds();
      UpdateCooldownLocked(time::SteadyTime::Now().ToMilliseconds());
      if (!pending.has_value()) continue;
      const vehicle::ChassisEvent& event = *pending;

      const bool duplicate = event.sequence <= status_.last_event_sequence;
      const bool stale = event.timestamp_ms > wall_now_ms ||
                         wall_now_ms - event.timestamp_ms > policy_.max_event_age.count();
      const bool unavailable = status_.state != SentinelState::kArmed;
      if (!duplicate) {
        status_.last_event_sequence = event.sequence;
        status_.last_event_timestamp_ms = event.timestamp_ms;
      }
      if (duplicate || stale || unavailable) {
        ++status_.suppressed_events;
        continue;
      }
      status_.state = SentinelState::kTriggered;
      status_.last_error.clear();
    }

    vehicle::ChassisEvent event = std::move(*pending);
    SentinelSnapshot snapshot;
    std::string error;
    const bool prepared = actions_->PrepareRecording(event.sequence, &error);
    const bool photo_ok = prepared && actions_->TakeSnapshot(event.sequence, &snapshot, &error);
    const bool record_ok = photo_ok && actions_->RecordMotion(event, snapshot, &error);

    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_ || status_.state == SentinelState::kDisabled) continue;
    if (prepared && photo_ok && record_ok) {
      ++status_.accepted_events;
      status_.last_snapshot_path = std::move(snapshot.path);
      status_.last_error.clear();
    } else {
      ++status_.failed_events;
      status_.last_error = error.empty() ? "sentinel trigger action failed" : std::move(error);
    }
    status_.state = SentinelState::kCooldown;
    status_.cooldown_until_ms = time::WallTime::Now().ToMilliseconds() + policy_.cooldown.count();
    cooldown_until_steady_ms_ = time::SteadyTime::Now().ToMilliseconds() + policy_.cooldown.count();
  }
}

}  // namespace sentinel
}  // namespace cockpit
