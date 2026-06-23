#include "modules/audio/audio_capture_stream.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using cockpit::audio::AudioCaptureSource;
using cockpit::audio::CaptureResult;
using cockpit::audio::CaptureStatus;

struct FakeState {
  std::mutex mutex;
  std::vector<CaptureResult> results;
  std::size_t next_result = 0;
  std::atomic_bool opened{false};
  std::atomic_bool closed{false};
  bool open_succeeds = true;
};

class FakeCaptureSource final : public AudioCaptureSource {
 public:
  explicit FakeCaptureSource(std::shared_ptr<FakeState> state) : state_(std::move(state)) {
  }

  bool Open(std::string* error) override {
    state_->opened.store(true);
    state_->closed.store(false);
    if (!state_->open_succeeds && error != nullptr) {
      *error = "fake open failed";
    }
    return state_->open_succeeds;
  }

  CaptureResult Read(std::int16_t* samples, std::size_t frame_capacity, int,
                     const std::atomic_bool& stop_requested) override {
    if (stop_requested.load()) {
      return {CaptureStatus::kStopped};
    }
    CaptureResult result{CaptureStatus::kTimeout};
    {
      std::lock_guard<std::mutex> lock(state_->mutex);
      if (state_->next_result < state_->results.size()) {
        result = state_->results[state_->next_result++];
      }
    }
    if (result.status == CaptureStatus::kOk) {
      if (result.frames_read > frame_capacity) {
        return result;
      }
      for (std::size_t index = 0; index < result.frames_read; ++index) {
        samples[index] = static_cast<std::int16_t>(index + 1U);
      }
    } else if (result.status == CaptureStatus::kTimeout) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return result;
  }

  void Close() override {
    state_->closed.store(true);
  }

 private:
  std::shared_ptr<FakeState> state_;
};

template <typename Predicate>
bool WaitUntil(const Predicate& predicate) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return predicate();
}

bool TestCaptureAndRecovery() {
  auto state = std::make_shared<FakeState>();
  state->results = {
      {CaptureStatus::kTimeout},
      {CaptureStatus::kXrunRecovered},
      {CaptureStatus::kOk, 160},
      {CaptureStatus::kOk, 160},
  };
  cockpit::audio::AudioCaptureStream stream(std::make_unique<FakeCaptureSource>(state));
  std::string error;
  if (!stream.Start(&error) || !WaitUntil([&stream] {
        return stream.metrics().audio_frames_published == 1;
      })) {
    std::cerr << "capture stream did not publish a frame: " << error << '\n';
    return false;
  }
  const auto frame = stream.TryPop();
  const auto metrics = stream.metrics();
  stream.Stop();
  return frame.has_value() && frame->sequence() == 0 &&
         frame->HasFlag(cockpit::audio::AudioFrameFlag::kDiscontinuity) &&
         frame->HasFlag(cockpit::audio::AudioFrameFlag::kRecoveredFromXrun) &&
         metrics.pcm_frames_read == 320 && metrics.timeouts >= 1 && metrics.xruns == 1 &&
         state->opened.load() && state->closed.load() &&
         stream.state() == cockpit::audio::AudioCaptureState::kStopped;
}

bool TestDeviceFailure() {
  auto state = std::make_shared<FakeState>();
  state->results = {
      {CaptureStatus::kDeviceError, 0, -1, "fake device failed"},
  };
  cockpit::audio::AudioCaptureStream stream(std::make_unique<FakeCaptureSource>(state));
  if (!stream.Start() || !WaitUntil([&stream] {
        return stream.state() == cockpit::audio::AudioCaptureState::kFaulted;
      })) {
    return false;
  }
  const bool fault_reported =
      stream.metrics().device_errors == 1 && stream.last_error() == "fake device failed";
  stream.Stop();
  return fault_reported && state->closed.load();
}

}  // namespace

int main() {
  if (!TestCaptureAndRecovery() || !TestDeviceFailure()) {
    return 1;
  }
  std::cout << "audio capture stream tests passed\n";
  return 0;
}
