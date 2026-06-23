#include "speech_output.h"

#include <exception>
#include <utility>

#include "core/logging/Logger.h"

namespace cockpit {
namespace audio {

SpeechOutput::SpeechOutput(std::string device,
                           std::unique_ptr<voice::SpeechSynthesizer> synthesizer,
                           std::unique_ptr<AudioPlayer> player)
    : device_(std::move(device)), synthesizer_(std::move(synthesizer)), player_(std::move(player)) {
}

SpeechOutput::~SpeechOutput() {
  Stop();
}

bool SpeechOutput::Start(std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (running_) {
    return true;
  }
  if (device_.empty() || synthesizer_ == nullptr || player_ == nullptr) {
    if (error != nullptr) {
      *error = "speech output requires a device, synthesizer, and player";
    }
    return false;
  }
  stop_requested_ = false;
  try {
    worker_ = std::thread(&SpeechOutput::Run, this);
  } catch (const std::exception& exception) {
    if (error != nullptr) {
      *error = exception.what();
    }
    return false;
  }
  running_ = true;
  return true;
}

void SpeechOutput::Stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) {
      return;
    }
    stop_requested_ = true;
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

bool SpeechOutput::Submit(std::string text) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!running_ || stop_requested_ || text.empty() || queue_.size() >= kQueueCapacity) {
    dropped_.fetch_add(1U);
    return false;
  }
  queue_.push_back(std::move(text));
  queued_.fetch_add(1U);
  changed_.notify_one();
  return true;
}

voice::VoiceOutputMetrics SpeechOutput::metrics() const {
  voice::VoiceOutputMetrics result;
  result.queued = queued_.load();
  result.played = played_.load();
  result.failed = failed_.load();
  result.dropped = dropped_.load();
  return result;
}

void SpeechOutput::Run() {
  while (true) {
    std::string text;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      changed_.wait(lock, [this] {
        return stop_requested_ || !queue_.empty();
      });
      if (queue_.empty() && stop_requested_) {
        break;
      }
      text = std::move(queue_.front());
      queue_.pop_front();
    }
    try {
      const voice::SpeechSynthesisResult synthesis = synthesizer_->Synthesize(text);
      std::string error;
      if (!synthesis.success || !player_->Play(device_, synthesis.audio, &error)) {
        failed_.fetch_add(1U);
        LOG_WARN("speech output failed error=" + (synthesis.success ? error : synthesis.error));
        continue;
      }
      played_.fetch_add(1U);
    } catch (const std::exception& exception) {
      failed_.fetch_add(1U);
      LOG_WARN(std::string("speech output exception error=") + exception.what());
    }
  }
}

}  // namespace audio
}  // namespace cockpit
