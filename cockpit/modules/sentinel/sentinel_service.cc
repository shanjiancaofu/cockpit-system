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
    : policy_(std::move(policy)), actions_(std::move(actions)) {
}

SentinelService::~SentinelService() {
  Stop();
}

bool SentinelService::Start(bool armed) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (running_ || actions_ == nullptr || policy_.cooldown.count() <= 0 ||
      policy_.max_event_age.count() <= 0 || policy_.queue_capacity == 0) {
    return false;
  }
  stopping_ = false;
  running_ = true;
  status_ = SentinelStatus{};
  status_.state = armed ? SentinelState::kArmed : SentinelState::kDisabled;
  worker_ = std::thread(&SentinelService::Run, this);
  return true;
}

void SentinelService::Stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) return;
    stopping_ = true;
    events_.clear();
  }
  actions_->Cancel();
  changed_.notify_all();
  if (worker_.joinable()) worker_.join();
  std::lock_guard<std::mutex> lock(mutex_);
  running_ = false;
  status_.state = SentinelState::kDisabled;
  status_.cooldown_until_ms = 0;
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
  status_.last_error.clear();
  changed_.notify_all();
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
    events_.clear();
  }
  actions_->Cancel();
  changed_.notify_all();
  return true;
}

bool SentinelService::Submit(vehicle::ChassisEvent event) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!running_ || stopping_ || status_.state == SentinelState::kDisabled ||
      event.type != vehicle::ChassisEventType::kMotionDetected || !event.motion_detected ||
      event.sequence == 0 || event.timestamp_ms <= 0)
    return false;
  if (events_.size() >= policy_.queue_capacity) {
    ++status_.dropped_events;
    status_.last_error = "sentinel event queue is full";
    return false;
  }
  events_.push_back(std::move(event));
  changed_.notify_one();
  return true;
}

SentinelStatus SentinelService::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return status_;
}

void SentinelService::UpdateCooldownLocked(std::int64_t now_ms) {
  if (status_.state == SentinelState::kCooldown && now_ms >= status_.cooldown_until_ms) {
    status_.state = SentinelState::kArmed;
    status_.cooldown_until_ms = 0;
  }
}

void SentinelService::Run() {
  while (true) {
    vehicle::ChassisEvent event;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      const auto wait_duration = status_.state == SentinelState::kCooldown
                                     ? std::chrono::milliseconds(std::max<std::int64_t>(
                                           1, status_.cooldown_until_ms - time::NowMs()))
                                     : std::chrono::milliseconds(100);
      changed_.wait_for(lock, wait_duration, [this] {
        return stopping_ || !events_.empty();
      });
      if (stopping_) return;
      const std::int64_t now_ms = time::NowMs();
      UpdateCooldownLocked(now_ms);
      if (events_.empty()) continue;
      event = std::move(events_.front());
      events_.pop_front();

      const bool duplicate = event.sequence <= status_.last_event_sequence;
      const bool stale = event.timestamp_ms > now_ms ||
                         now_ms - event.timestamp_ms > policy_.max_event_age.count();
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
    status_.cooldown_until_ms = time::NowMs() + policy_.cooldown.count();
  }
}

}  // namespace sentinel
}  // namespace cockpit
