#include "cockpit/library/driver/audio/playback/audio_playback.h"

#include <exception>
#include <utility>

#include "cockpit/core/logging/logger.h"

namespace cockpit {
namespace audio {

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
    dropped_.fetch_add(static_cast<std::uint64_t>(queue_.size()));
    queue_.clear();
  }
  changed_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  running_ = false;
}

bool AudioPlayback::Submit(PcmBuffer buffer) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!running_ || stop_requested_.load() || buffer.samples.empty() ||
      queue_.size() >= kQueueCapacity) {
    dropped_.fetch_add(1U);
    return false;
  }
  queue_.push_back(std::move(buffer));
  queued_.fetch_add(1U);
  changed_.notify_one();
  return true;
}

AudioPlaybackMetrics AudioPlayback::metrics() const {
  return {queued_.load(), played_.load(), failed_.load(), dropped_.load()};
}

void AudioPlayback::Run() {
  while (true) {
    PcmBuffer buffer;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      changed_.wait(lock, [this] {
        return stop_requested_.load() || !queue_.empty();
      });
      if (queue_.empty() && stop_requested_.load()) {
        break;
      }
      buffer = std::move(queue_.front());
      queue_.pop_front();
    }
    try {
      std::string error;
      if (!player_->Play(device_, buffer, stop_requested_, &error)) {
        if (stop_requested_.load()) {
          dropped_.fetch_add(1U);
          break;
        }
        failed_.fetch_add(1U);
        LOG_WARN("audio playback failed error=" + error);
        continue;
      }
      played_.fetch_add(1U);
    } catch (const std::exception& exception) {
      failed_.fetch_add(1U);
      LOG_WARN(std::string("audio playback exception error=") + exception.what());
    }
  }
}

}  // namespace audio
}  // namespace cockpit
