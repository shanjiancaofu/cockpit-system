#include "cockpit/library/driver/audio/capture/audio_capture_controller.h"

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace {

struct FakeState {
  std::atomic_bool opened{false};
  std::atomic_bool closed{false};
};

class FakeCaptureSource final : public cockpit::audio::AudioCaptureSource {
 public:
  explicit FakeCaptureSource(std::shared_ptr<FakeState> state) : state_(std::move(state)) {
  }

  bool Open(std::string*) override {
    state_->opened.store(true);
    state_->closed.store(false);
    return true;
  }

  cockpit::audio::CaptureResult Read(std::int16_t* samples, std::size_t frame_capacity, int,
                                     const std::atomic_bool& stop_requested) override {
    if (stop_requested.load()) {
      return {cockpit::audio::CaptureStatus::kStopped, 0, 0, {}};
    }
    for (std::size_t index = 0; index < frame_capacity; ++index) {
      samples[index] = static_cast<std::int16_t>(index);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return {cockpit::audio::CaptureStatus::kOk, frame_capacity, 0, {}};
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

}  // namespace

int main() {
  const std::string socket_path =
      "/tmp/cockpit-audio-controller-" + std::to_string(::getpid()) + ".sock";
  cockpit::audio::AudioStreamPublisher publisher;
  std::string error;
  if (!publisher.Start(socket_path, &error)) {
    std::cerr << "failed to start stream publisher: " << error << '\n';
    return 1;
  }

  cockpit::config::AudioConfig config;
  config.input_device = "fake-input";
  auto fake_state = std::make_shared<FakeState>();
  cockpit::audio::AudioCaptureController controller(
      config,
      [fake_state](const std::string&, const cockpit::audio::PcmFormat&) {
        return std::make_unique<FakeCaptureSource>(fake_state);
      },
      publisher);
  if (!controller.Start("", &error)) {
    std::cerr << "failed to start audio capture controller: " << error << '\n';
    return 1;
  }
  if (controller.Start("", &error) || error != "audio capture is already active") {
    std::cerr << "duplicate capture start was not rejected\n";
    return 1;
  }
  if (!WaitUntil([&controller] {
        return controller.status().capture_metrics.audio_frames_published >= 3 &&
               controller.status().stream_metrics.frames_queued >= 3 &&
               controller.status().input_level_dbfs > -120.0;
      })) {
    std::cerr << "captured PCM was not published\n";
    return 1;
  }
  controller.Stop();
  publisher.Stop();
  const auto status = controller.status();
  if (status.capture_state != cockpit::audio::AudioCaptureState::kStopped ||
      status.input_device != "fake-input" || status.capture_metrics.audio_frames_published < 3 ||
      !fake_state->opened.load() || !fake_state->closed.load() || status.modules.size() != 1 ||
      status.modules[0].name != "audio-capture" ||
      status.modules[0].state != cockpit::runtime::ModuleState::kStopped) {
    std::cerr << "audio capture controller stopped status is invalid\n";
    return 1;
  }
  std::cout << "audio capture controller tests passed\n";
  return 0;
}
