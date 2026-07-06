#include "cockpit/modules/audio/capture/audio_capture_stream.h"

#include <chrono>
#include <exception>
#include <utility>

namespace cockpit {
namespace audio {
namespace {

constexpr int kReadTimeoutMs = 100;

std::int64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

}  // namespace

AudioCaptureStream::AudioCaptureStream(std::unique_ptr<AudioCaptureSource> source)
    : source_(std::move(source)) {
}

AudioCaptureStream::~AudioCaptureStream() {
  Stop();
}

bool AudioCaptureStream::Start(std::string* error) {
  if (worker_.joinable()) {
    const AudioCaptureState current = state_.load();
    if (current != AudioCaptureState::kFaulted && current != AudioCaptureState::kStopped) {
      if (error != nullptr) {
        *error = "audio capture stream worker is still active";
      }
      return false;
    }
    worker_.join();
  }

  AudioCaptureState expected = AudioCaptureState::kStopped;
  if (state_.load() == AudioCaptureState::kFaulted) {
    expected = AudioCaptureState::kFaulted;
  }
  if (source_ == nullptr) {
    if (error != nullptr) {
      *error = "audio capture source is null";
    }
    return false;
  }
  if (!state_.compare_exchange_strong(expected, AudioCaptureState::kStarting)) {
    if (error != nullptr) {
      *error = "audio capture stream is already active";
    }
    return false;
  }

  buffer_.Reset();
  ResetMetrics();
  SetError({});
  stop_requested_.store(false);
  try {
    worker_ = std::thread(&AudioCaptureStream::Run, this);
  } catch (const std::exception& exception) {
    state_.store(AudioCaptureState::kStopped);
    if (error != nullptr) {
      *error = exception.what();
    }
    return false;
  }
  return true;
}

void AudioCaptureStream::Stop() {
  stop_requested_.store(true);
  if (worker_.joinable()) {
    worker_.join();
  }
  state_.store(AudioCaptureState::kStopped);
}

std::optional<AudioFrame> AudioCaptureStream::TryPop() {
  return buffer_.TryPop();
}

AudioCaptureMetrics AudioCaptureStream::metrics() const {
  AudioCaptureMetrics result;
  result.pcm_frames_read = pcm_frames_read_.load();
  result.audio_frames_published = audio_frames_published_.load();
  result.audio_frames_dropped = buffer_.DropCount();
  result.timeouts = timeouts_.load();
  result.xruns = xruns_.load();
  result.device_errors = device_errors_.load();
  return result;
}

std::string AudioCaptureStream::last_error() const {
  std::lock_guard<std::mutex> lock(error_mutex_);
  return last_error_;
}

void AudioCaptureStream::Run() {
  std::string error;
  if (!source_->Open(&error)) {
    device_errors_.fetch_add(1U);
    SetError(std::move(error));
    state_.store(AudioCaptureState::kFaulted);
    source_->Close();
    return;
  }
  state_.store(AudioCaptureState::kRunning);

  AudioFrame::Samples samples{};
  std::size_t sample_count = 0;
  std::uint64_t sequence = 0;
  AudioFrameFlag pending_flags = AudioFrameFlag::kNone;
  while (!stop_requested_.load()) {
    CaptureResult result =
        source_->Read(samples.data() + sample_count, samples.size() - sample_count, kReadTimeoutMs,
                      stop_requested_);
    switch (result.status) {
      case CaptureStatus::kOk:
        if (result.frames_read == 0 || result.frames_read > samples.size() - sample_count) {
          device_errors_.fetch_add(1U);
          SetError("audio source returned an invalid frame count");
          state_.store(AudioCaptureState::kFaulted);
          stop_requested_.store(true);
          break;
        }
        pcm_frames_read_.fetch_add(result.frames_read);
        sample_count += result.frames_read;
        if (sample_count == samples.size()) {
          AudioFrame frame(sequence, NowNs(), pending_flags, samples);
          if (buffer_.TryPush(std::move(frame))) {
            audio_frames_published_.fetch_add(1U);
            pending_flags = AudioFrameFlag::kNone;
          } else {
            pending_flags = AudioFrameFlag::kDiscontinuity | AudioFrameFlag::kDroppedBefore;
          }
          ++sequence;
          sample_count = 0;
        }
        break;
      case CaptureStatus::kTimeout:
        timeouts_.fetch_add(1U);
        break;
      case CaptureStatus::kXrunRecovered:
        xruns_.fetch_add(1U);
        sample_count = 0;
        pending_flags =
            pending_flags | AudioFrameFlag::kDiscontinuity | AudioFrameFlag::kRecoveredFromXrun;
        state_.store(AudioCaptureState::kRecovering);
        state_.store(AudioCaptureState::kRunning);
        break;
      case CaptureStatus::kStopped:
        stop_requested_.store(true);
        break;
      case CaptureStatus::kDeviceError:
        device_errors_.fetch_add(1U);
        SetError(result.message.empty() ? "audio capture device failed"
                                        : std::move(result.message));
        state_.store(AudioCaptureState::kFaulted);
        stop_requested_.store(true);
        break;
    }
  }

  source_->Close();
  if (state_.load() != AudioCaptureState::kFaulted) {
    state_.store(AudioCaptureState::kStopped);
  }
}

void AudioCaptureStream::SetError(std::string error) {
  std::lock_guard<std::mutex> lock(error_mutex_);
  last_error_ = std::move(error);
}

void AudioCaptureStream::ResetMetrics() {
  pcm_frames_read_.store(0);
  audio_frames_published_.store(0);
  timeouts_.store(0);
  xruns_.store(0);
  device_errors_.store(0);
}

}  // namespace audio
}  // namespace cockpit
