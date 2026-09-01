#include "cockpit/modules/camera/capture/software_isp_preview_source.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace {

struct FakeState {
  std::atomic<int> created{0};
};

class FakeCapture final : public cockpit::camera::SoftwareIspCapture {
 public:
  FakeCapture(std::shared_ptr<FakeState> state, int index)
      : state_(std::move(state)), index_(index) {
  }

  bool Start(const cockpit::camera::V4l2MmapConfig&, std::string* error) override {
    running_ = index_ == 0 || index_ >= 4;
    if (!running_ && error != nullptr) *error = "injected reopen failure";
    return running_;
  }
  bool WaitFrame(cockpit::camera::V4l2RawFrame* frame, int, std::string* error) override {
    if (index_ == 0) {
      running_ = false;
      if (error != nullptr) *error = "injected fatal capture";
      return false;
    }
    if (index_ >= 4 && !delivered_) {
      delivered_ = true;
      frame->width = 2;
      frame->height = 2;
      frame->bytes_per_line = 4;
      frame->bytes_used = 8;
      frame->sequence = 1;
      frame->data.assign(8, 0);
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return false;
  }
  void Stop() override {
    running_ = false;
  }
  bool running() const override {
    return running_;
  }

 private:
  std::shared_ptr<FakeState> state_;
  int index_;
  bool running_ = false;
  bool delivered_ = false;
};

}  // namespace

int main() {
  cockpit::camera::SoftwareIspPreviewSource source;
  std::string error;
  assert(!source.Start(cockpit::camera::CameraPreviewConfig{}, {}, &error));
  const auto stats = source.stats();
  assert(stats.fatal_capture_errors == 0);
  assert(stats.last_error.empty());

  auto state = std::make_shared<FakeState>();
  cockpit::camera::SoftwareIspPreviewSource recovering_source([state] {
    const int index = state->created.fetch_add(1);
    return std::make_unique<FakeCapture>(state, index);
  });
  cockpit::camera::CameraPreviewConfig config;
  config.device = "/dev/video0";
  config.width = 2;
  config.height = 2;
  config.fps = 30;
  assert(recovering_source.Start(
      config,
      [](cockpit::camera::CameraFrame) {
      },
      &error));
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (recovering_source.stats().reconnect_successes == 0 &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  const auto recovery_stats = recovering_source.stats();
  assert(recovery_stats.fatal_capture_errors == 1);
  assert(recovery_stats.reconnect_attempts == 4);
  assert(recovery_stats.reconnect_successes == 1);
  assert(recovery_stats.consecutive_failures == 0);
  recovering_source.Stop();
  assert(!recovering_source.IsRunning());

  auto failing_state = std::make_shared<FakeState>();
  cockpit::camera::SoftwareIspPreviewSource failing_source([failing_state] {
    const int index = failing_state->created.fetch_add(1);
    return std::make_unique<FakeCapture>(failing_state, index == 0 ? 0 : 1);
  });
  assert(failing_source.Start(
      config,
      [](cockpit::camera::CameraFrame) {
      },
      &error));
  const auto recovering_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (!failing_source.IsRecovering() && std::chrono::steady_clock::now() < recovering_deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  assert(failing_source.IsRecovering());
  const auto stop_started = std::chrono::steady_clock::now();
  failing_source.Stop();
  const auto stop_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - stop_started);
  assert(stop_elapsed < std::chrono::milliseconds(500));
  assert(!failing_source.IsRunning());
  return 0;
}
