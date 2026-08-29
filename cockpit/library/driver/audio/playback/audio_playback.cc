#include "cockpit/library/driver/audio/playback/audio_playback.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <utility>

#include "cockpit/core/logging/logger.h"

namespace cockpit {
namespace audio {

struct AudioPlayback::PlaybackRequest {
  std::uint64_t playback_id = 0;
  PcmBuffer buffer;
  std::atomic_bool cancelled{false};
};

AudioPlayback::AudioPlayback(std::string device, std::unique_ptr<AudioPlayer> player)
    : device_(std::move(device)), player_(std::move(player)) {
}

AudioPlayback::~AudioPlayback() {
  Stop();
}

bool AudioPlayback::Start(std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (running_) {
    return true;
  }
  if (device_.empty() || player_ == nullptr) {
    if (error != nullptr) {
      *error = "audio playback requires a device and player";
    }
    return false;
  }
  stop_requested_.store(false);
  try {
    worker_ = std::thread(&AudioPlayback::Run, this);
  } catch (const std::exception& exception) {
    if (error != nullptr) {
      *error = exception.what();
    }
    return false;
  }
  running_ = true;
  return true;
}

void AudioPlayback::Stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) {
      return;
    }
    stop_requested_.store(true);
    for (const auto& request : queue_) {
      FinalizeLocked(request->playback_id, AudioPlaybackStatus::kDropped,
                     "audio playback stopped before starting");
      dropped_.fetch_add(1U);
    }
    queue_.clear();
    if (active_ != nullptr) {
      active_->cancelled.store(true);
    }
  }
  changed_.notify_all();
  result_changed_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  running_ = false;
}

std::optional<std::uint64_t> AudioPlayback::Submit(PcmBuffer buffer, std::uint64_t playback_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!running_ || stop_requested_.load() || buffer.samples.empty() ||
      queue_.size() >= kQueueCapacity) {
    dropped_.fetch_add(1U);
    return std::nullopt;
  }
  if (playback_id == 0U) {
    do {
      playback_id = next_playback_id_++;
    } while (playback_id == 0U || results_.find(playback_id) != results_.end());
  } else if (results_.find(playback_id) != results_.end()) {
    dropped_.fetch_add(1U);
    return std::nullopt;
  }
  auto request = std::make_shared<PlaybackRequest>();
  request->playback_id = playback_id;
  request->buffer = std::move(buffer);
  results_.emplace(playback_id,
                   AudioPlaybackResult{playback_id, AudioPlaybackStatus::kPending, {}});
  queue_.push_back(std::move(request));
  queued_.fetch_add(1U);
  changed_.notify_one();
  return playback_id;
}

bool AudioPlayback::Cancel(std::uint64_t playback_id) {
  if (playback_id == 0U) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto result = results_.find(playback_id);
  if (result != results_.end() && result->second.status != AudioPlaybackStatus::kPending) {
    return result->second.status == AudioPlaybackStatus::kCancelled;
  }
  if (active_ != nullptr && active_->playback_id == playback_id) {
    active_->cancelled.store(true);
    return true;
  }
  const auto queued =
      std::find_if(queue_.begin(), queue_.end(), [playback_id](const auto& request) {
        return request->playback_id == playback_id;
      });
  if (queued != queue_.end()) {
    queue_.erase(queued);
    FinalizeLocked(playback_id, AudioPlaybackStatus::kCancelled,
                   "audio playback cancelled before starting");
    dropped_.fetch_add(1U);
    result_changed_.notify_all();
    return true;
  }

  // Preserve a bounded cancellation tombstone so a concurrent PlayPcm cannot enqueue
  // after its CancelPlayback request has already arrived.
  results_[playback_id] = AudioPlaybackResult{playback_id, AudioPlaybackStatus::kCancelled,
                                              "audio playback cancelled before submission"};
  final_result_ids_.push_back(playback_id);
  while (final_result_ids_.size() > kResultCapacity) {
    results_.erase(final_result_ids_.front());
    final_result_ids_.pop_front();
  }
  result_changed_.notify_all();
  return true;
}

AudioPlaybackWaitStatus AudioPlayback::WaitForResult(std::uint64_t playback_id,
                                                     std::chrono::milliseconds timeout,
                                                     AudioPlaybackResult* result) {
  if (playback_id == 0U || timeout < std::chrono::milliseconds::zero()) {
    return AudioPlaybackWaitStatus::kNotFound;
  }
  std::unique_lock<std::mutex> lock(mutex_);
  auto found = results_.find(playback_id);
  if (found == results_.end()) {
    return AudioPlaybackWaitStatus::kNotFound;
  }
  if (found->second.status == AudioPlaybackStatus::kPending) {
    result_changed_.wait_until(lock, std::chrono::steady_clock::now() + timeout,
                               [this, playback_id] {
                                 const auto current = results_.find(playback_id);
                                 return current == results_.end() ||
                                        current->second.status != AudioPlaybackStatus::kPending;
                               });
    found = results_.find(playback_id);
    if (found == results_.end()) {
      return AudioPlaybackWaitStatus::kNotFound;
    }
    if (found->second.status == AudioPlaybackStatus::kPending) {
      return AudioPlaybackWaitStatus::kTimeout;
    }
  }
  if (result != nullptr) {
    *result = found->second;
  }
  return AudioPlaybackWaitStatus::kReady;
}

AudioPlaybackMetrics AudioPlayback::metrics() const {
  return {queued_.load(), played_.load(), failed_.load(), dropped_.load()};
}

void AudioPlayback::Run() {
  while (true) {
    std::shared_ptr<PlaybackRequest> request;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      changed_.wait(lock, [this] {
        return stop_requested_.load() || !queue_.empty();
      });
      if (queue_.empty() && stop_requested_.load()) {
        break;
      }
      request = std::move(queue_.front());
      queue_.pop_front();
      active_ = request;
    }
    bool succeeded = false;
    std::string error;
    try {
      succeeded = player_->Play(device_, request->buffer, request->cancelled, &error);
    } catch (const std::exception& exception) {
      error = exception.what();
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (request->cancelled.load()) {
        FinalizeLocked(request->playback_id, AudioPlaybackStatus::kCancelled,
                       error.empty() ? "audio playback cancelled" : error);
        dropped_.fetch_add(1U);
      } else if (succeeded) {
        FinalizeLocked(request->playback_id, AudioPlaybackStatus::kCompleted, {});
        played_.fetch_add(1U);
      } else {
        FinalizeLocked(request->playback_id, AudioPlaybackStatus::kFailed,
                       error.empty() ? "audio playback failed" : error);
        failed_.fetch_add(1U);
        LOG_WARN("audio playback failed error=" + error);
      }
      active_.reset();
    }
    result_changed_.notify_all();
  }
}

void AudioPlayback::FinalizeLocked(std::uint64_t playback_id, AudioPlaybackStatus status,
                                   std::string error) {
  const auto found = results_.find(playback_id);
  if (found == results_.end() || found->second.status != AudioPlaybackStatus::kPending) {
    return;
  }
  found->second.status = status;
  found->second.error = std::move(error);
  final_result_ids_.push_back(playback_id);
  while (final_result_ids_.size() > kResultCapacity) {
    const std::uint64_t oldest = final_result_ids_.front();
    final_result_ids_.pop_front();
    const auto oldest_result = results_.find(oldest);
    if (oldest_result != results_.end() &&
        oldest_result->second.status != AudioPlaybackStatus::kPending) {
      results_.erase(oldest_result);
    }
  }
}

}  // namespace audio
}  // namespace cockpit
