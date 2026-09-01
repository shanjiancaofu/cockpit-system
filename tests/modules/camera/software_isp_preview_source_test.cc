#include "cockpit/modules/camera/capture/software_isp_preview_source.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef NDEBUG
#undef assert
#define assert(expression) ((expression) ? static_cast<void>(0) : std::abort())
#endif

namespace {

using namespace std::chrono_literals;

struct BackendControl {
  bool start_success = true;
  bool fatal_after_frame = false;
  std::atomic_bool allow_frame{false};
  std::atomic_bool fail_now{false};
  std::atomic_bool delivered{false};
};

struct CaptureScript {
  std::vector<std::shared_ptr<BackendControl>> backends;
  std::atomic<int> created{0};
};

void FillFrame(cockpit::camera::V4l2RawFrame* frame, std::uint32_t sequence) {
  frame->width = 2;
  frame->height = 2;
  frame->bytes_per_line = 4;
  frame->bytes_used = 8;
  frame->sequence = sequence;
  frame->data.assign(8, 0);
}

class ScriptedCapture final : public cockpit::camera::SoftwareIspCapture {
 public:
  ScriptedCapture(std::shared_ptr<BackendControl> control, int index)
      : control_(std::move(control)), index_(index) {
  }

  bool Start(const cockpit::camera::V4l2MmapConfig&, std::string* error) override {
    running_ = control_->start_success;
    if (!running_ && error != nullptr) *error = "injected reopen failure";
    return running_;
  }

  bool WaitFrame(cockpit::camera::V4l2RawFrame* frame, int, std::string* error) override {
    if (!running_) return false;
    if (control_->allow_frame.load() && !control_->delivered.exchange(true)) {
      FillFrame(frame, static_cast<std::uint32_t>(index_ + 1));
      return true;
    }
    if (control_->fail_now.load() || (control_->fatal_after_frame && control_->delivered.load())) {
      running_ = false;
      if (error != nullptr) *error = "injected fatal capture";
      return false;
    }
    std::this_thread::sleep_for(5ms);
    return false;
  }

  void Stop() override {
    running_ = false;
  }
  bool running() const override {
    return running_;
  }

 private:
  std::shared_ptr<BackendControl> control_;
  int index_;
  std::atomic_bool running_{false};
};

cockpit::camera::SoftwareIspPreviewSource::CaptureFactory MakeFactory(
    const std::shared_ptr<CaptureScript>& script) {
  return [script] {
    const int index = script->created.fetch_add(1);
    const auto selected =
        std::min<std::size_t>(static_cast<std::size_t>(index), script->backends.size() - 1U);
    return std::make_unique<ScriptedCapture>(script->backends[selected], index);
  };
}

struct ProcessorControl {
  std::atomic<int> calls{0};
  std::atomic_bool fail{false};
  std::atomic_bool block_first{false};
  std::atomic_bool first_entered{false};
  std::atomic_bool release_first{false};
  std::chrono::milliseconds delay{0};
};

class ControlledProcessor final : public cockpit::camera::SoftwareIspProcessor {
 public:
  explicit ControlledProcessor(std::shared_ptr<ProcessorControl> control)
      : control_(std::move(control)) {
  }

  bool Process(const cockpit::camera::RawBayerFrame&, cockpit::camera::CameraFrame*,
               std::string* error, cockpit::camera::SoftwareIspTimingMs*) override {
    const int call = control_->calls.fetch_add(1) + 1;
    if (call == 1 && control_->block_first.load()) {
      control_->first_entered.store(true);
      while (!control_->release_first.load()) std::this_thread::sleep_for(1ms);
    }
    if (control_->delay.count() > 0) std::this_thread::sleep_for(control_->delay);
    if (control_->fail.load()) {
      if (error != nullptr) *error = "injected ISP failure";
      return false;
    }
    return true;
  }

 private:
  std::shared_ptr<ProcessorControl> control_;
};

cockpit::camera::CameraPreviewConfig ValidConfig() {
  cockpit::camera::CameraPreviewConfig config;
  config.device = "/dev/video0";
  config.width = 2;
  config.height = 2;
  config.fps = 30;
  return config;
}

template <typename Predicate>
bool WaitUntil(std::chrono::milliseconds timeout, Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!predicate() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(5ms);
  }
  return predicate();
}

std::shared_ptr<BackendControl> Backend(bool frame, bool fatal_after = false) {
  auto backend = std::make_shared<BackendControl>();
  backend->allow_frame.store(frame);
  backend->fatal_after_frame = fatal_after;
  return backend;
}

void TestSilentBackendRetries() {
  auto initial = Backend(false);
  initial->fail_now.store(true);
  auto silent = Backend(false);
  auto script = std::make_shared<CaptureScript>();
  script->backends = {initial, silent};
  auto processor = std::make_shared<ProcessorControl>();
  cockpit::camera::SoftwareIspPreviewSource source(
      MakeFactory(script), std::make_unique<ControlledProcessor>(processor));
  std::string error;
  assert(source.Start(
      ValidConfig(),
      [](cockpit::camera::CameraFrame) {
      },
      &error));
  assert(WaitUntil(2500ms, [&] {
    return source.stats().fatal_capture_errors >= 2;
  }));
  assert(source.stats().reconnect_successes == 0);
  source.Stop();
}

void TestOldGenerationCannotCompleteRecovery() {
  auto old_backend = Backend(true, true);
  auto new_backend = Backend(false);
  auto script = std::make_shared<CaptureScript>();
  script->backends = {old_backend, new_backend};
  auto processor = std::make_shared<ProcessorControl>();
  processor->block_first.store(true);
  std::atomic<int> delivered{0};
  cockpit::camera::SoftwareIspPreviewSource source(
      MakeFactory(script), std::make_unique<ControlledProcessor>(processor));
  std::string error;
  assert(source.Start(
      ValidConfig(),
      [&](cockpit::camera::CameraFrame) {
        ++delivered;
      },
      &error));
  assert(WaitUntil(500ms, [&] {
    return processor->first_entered.load();
  }));
  assert(WaitUntil(1000ms, [&] {
    return script->created.load() >= 2;
  }));
  processor->release_first.store(true);
  std::this_thread::sleep_for(30ms);
  assert(source.stats().reconnect_successes == 0);
  assert(delivered.load() == 0);
  new_backend->allow_frame.store(true);
  assert(WaitUntil(500ms, [&] {
    return source.stats().reconnect_successes == 1;
  }));
  assert(delivered.load() == 1);
  source.Stop();
}

void TestNewGenerationFirstFrameSucceeds() {
  auto initial = Backend(false);
  initial->fail_now.store(true);
  auto recovered = Backend(true);
  auto script = std::make_shared<CaptureScript>();
  script->backends = {initial, recovered};
  auto processor = std::make_shared<ProcessorControl>();
  cockpit::camera::SoftwareIspPreviewSource source(
      MakeFactory(script), std::make_unique<ControlledProcessor>(processor));
  std::string error;
  assert(source.Start(
      ValidConfig(),
      [](cockpit::camera::CameraFrame) {
      },
      &error));
  assert(WaitUntil(1000ms, [&] {
    return source.stats().reconnect_successes == 1;
  }));
  assert(source.stats().consecutive_failures == 0);
  assert(!source.IsRecovering());
  source.Stop();
}

void TestIspFailureAndLateSuccessDoNotRecover() {
  for (const bool late_success : {false, true}) {
    auto initial = Backend(false);
    initial->fail_now.store(true);
    auto recovered = Backend(true);
    auto script = std::make_shared<CaptureScript>();
    script->backends = {initial, recovered};
    auto processor = std::make_shared<ProcessorControl>();
    processor->fail.store(!late_success);
    if (late_success) processor->delay = 2100ms;
    cockpit::camera::SoftwareIspPreviewSource source(
        MakeFactory(script), std::make_unique<ControlledProcessor>(processor));
    std::string error;
    assert(source.Start(
        ValidConfig(),
        [](cockpit::camera::CameraFrame) {
        },
        &error));
    assert(WaitUntil(2500ms, [&] {
      return source.stats().fatal_capture_errors >= 2;
    }));
    assert(source.stats().reconnect_successes == 0);
    source.Stop();
  }
}

void TestStopDuringRecoveryIsBounded() {
  auto initial = Backend(false);
  initial->fail_now.store(true);
  auto failed_reopen = Backend(false);
  failed_reopen->start_success = false;
  auto script = std::make_shared<CaptureScript>();
  script->backends = {initial, failed_reopen};
  auto processor = std::make_shared<ProcessorControl>();
  cockpit::camera::SoftwareIspPreviewSource source(
      MakeFactory(script), std::make_unique<ControlledProcessor>(processor));
  std::string error;
  assert(source.Start(
      ValidConfig(),
      [](cockpit::camera::CameraFrame) {
      },
      &error));
  assert(WaitUntil(500ms, [&] {
    return source.IsRecovering();
  }));
  const auto started = std::chrono::steady_clock::now();
  source.Stop();
  assert(std::chrono::steady_clock::now() - started < 500ms);
  assert(!source.IsRunning());
}

void TestSecondFaultRestartsAtMinimumBackoff() {
  auto initial = Backend(false);
  initial->fail_now.store(true);
  auto first_recovery = Backend(true);
  auto second_recovery = Backend(true);
  auto script = std::make_shared<CaptureScript>();
  script->backends = {initial, first_recovery, second_recovery};
  auto processor = std::make_shared<ProcessorControl>();
  cockpit::camera::SoftwareIspPreviewSource source(
      MakeFactory(script), std::make_unique<ControlledProcessor>(processor));
  std::string error;
  assert(source.Start(
      ValidConfig(),
      [](cockpit::camera::CameraFrame) {
      },
      &error));
  assert(WaitUntil(1000ms, [&] {
    return source.stats().reconnect_successes == 1;
  }));
  first_recovery->fail_now.store(true);
  assert(WaitUntil(1000ms, [&] {
    return source.stats().reconnect_successes == 2;
  }));
  assert(source.stats().last_reconnect_backoff_ms == 100);
  assert(source.stats().consecutive_failures == 0);
  source.Stop();
}

}  // namespace

int main() {
  cockpit::camera::SoftwareIspPreviewSource invalid_source;
  std::string error;
  assert(!invalid_source.Start(cockpit::camera::CameraPreviewConfig{}, {}, &error));
  assert(invalid_source.stats().fatal_capture_errors == 0);
  TestSilentBackendRetries();
  TestOldGenerationCannotCompleteRecovery();
  TestNewGenerationFirstFrameSucceeds();
  TestIspFailureAndLateSuccessDoNotRecover();
  TestStopDuringRecoveryIsBounded();
  TestSecondFaultRestartsAtMinimumBackoff();
  return 0;
}
