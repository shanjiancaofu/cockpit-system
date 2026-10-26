#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "cockpit/driver/camera/capture/native_argus_capture.h"
#include "tools/camera/camera-diag/diagnostics.h"

namespace cam = cockpit::camera;
namespace diag = cockpit::camera_diag;
namespace {
void Require(bool ok, const std::string& why) {
  if (!ok) throw std::runtime_error(why);
}
void Wait(const std::atomic<std::uint32_t>& observed, std::uint32_t revision) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (observed.load() < revision && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  Require(observed.load() == revision, "worker did not observe committed revision");
}
}  // namespace
// Hardware-only; explicitly invoked by the Jetson gate, never automatic CTest.
int main(int argc, char** argv) {
  if (argc != 3 || std::string(argv[1]) != "--output") return 2;
  cam::NativeArgusCapture capture;
  cam::FrameLease held;
  bool concurrent = false, held_unchanged = false, deferred_stop = false, fresh_session = false;
  std::string error;
  try {
    Require(capture.Start({}, &error), error);
    Require(capture.Acquire(&held, 5000, &error) == cam::NativeAcquireStatus::kFrame, error);
    const auto old_session = held.frame().session_id;
    const auto old_metadata = held.frame().tuning;
    cam::CameraTuningState state;
    Require(capture.GetTuning(&state, &error), error);
    auto manual = state.defaults;
    manual.ae_mode = "manual";
    manual.ae_lock = false;
    manual.exposure_compensation_ev = 0;
    manual.exposure_ns = {8000000, 8000000};
    manual.sensor_gain = {2, 2};
    manual.isp_gain = {1, 1};
    std::atomic<bool> stop{false};
    std::atomic<std::uint32_t> observed{0};
    std::string worker_error;
    std::thread worker([&] {
      try {
        std::uint32_t previous = held.frame().capture_id;
        std::uint64_t timestamp = held.frame().sensor_timestamp_ns;
        while (!stop.load()) {
          cam::FrameLease lease;
          std::string why;
          Require(capture.Acquire(&lease, 1000, &why) == cam::NativeAcquireStatus::kFrame, why);
          const auto& frame = lease.frame();
          Require(frame.session_id == old_session && frame.capture_id > previous &&
                      frame.sensor_timestamp_ns > timestamp && frame.tuning.valid,
                  "metadata regression");
          previous = frame.capture_id;
          timestamp = frame.sensor_timestamp_ns;
          if (frame.tuning.revision == 2 || frame.tuning.revision == 3) {
            const float gain = frame.tuning.revision == 2 ? 2.0f : 3.0f;
            Require(std::abs(static_cast<double>(frame.tuning.exposure_ns) - 8000000) <= 100000 &&
                        std::abs(frame.tuning.sensor_gain - gain) <= gain * 0.05f,
                    "metadata does not belong to its revision");
          }
          observed.store(frame.tuning.revision);
          lease.Reset();
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
      } catch (const std::exception& e) {
        worker_error = e.what();
      }
    });
    try {
      Wait(observed, 1);
      std::uint32_t revision = 0;
      Require(capture.SetTuning(manual, &revision, &error) && revision == 2, error);
      Wait(observed, 2);
      manual.sensor_gain = {3, 3};
      Require(capture.SetTuning(manual, &revision, &error) && revision == 3, error);
      Wait(observed, 3);
      Require(capture.ResetTuning(&revision, &error) && revision == 4, error);
      Wait(observed, 4);
      stop.store(true);
      worker.join();
      Require(worker_error.empty(), worker_error);
      concurrent = true;
    } catch (...) {
      stop.store(true);
      if (worker.joinable()) worker.join();
      throw;
    }
    held_unchanged = held.frame().tuning.revision == old_metadata.revision &&
                     held.frame().tuning.exposure_ns == old_metadata.exposure_ns;
    Require(held_unchanged, "old lease metadata changed after control update");
    deferred_stop = capture.Stop(&error) == cam::NativeStopStatus::kLeasesOutstanding;
    Require(deferred_stop && !capture.SetTuning(manual, nullptr, &error),
            "control accepted while stopping");
    held.Reset();
    Require(capture.Stop(&error) == cam::NativeStopStatus::kStopped, error);
    Require(capture.stats().ResourcesBalanced(), "unbalanced first session");
    Require(capture.Start({}, &error), error);
    Require(capture.GetTuning(&state, &error), error);
    fresh_session = state.session_id != old_session && state.revision == 1;
    Require(fresh_session, "tuning state leaked between sessions");
    Require(capture.Acquire(&held, 5000, &error) == cam::NativeAcquireStatus::kFrame, error);
    Require(held.frame().tuning.revision == 1, "old request reached new session");
    held.Reset();
    Require(capture.Stop(&error) == cam::NativeStopStatus::kStopped, error);
    error.clear();
  } catch (const std::exception& e) {
    error = e.what();
  }
  held.Reset();
  std::string why;
  if (capture.Stop(&why) != cam::NativeStopStatus::kStopped) error += " stop: " + why;
  const auto stats = capture.stats();
  const bool passed = error.empty() && concurrent && held_unchanged && deferred_stop &&
                      fresh_session && stats.ResourcesBalanced();
  const auto report =
      diag::Object({{"passed", diag::Boolean(passed)},
                    {"concurrent_acquire_and_control", diag::Boolean(concurrent)},
                    {"held_metadata_unchanged", diag::Boolean(held_unchanged)},
                    {"deferred_stop", diag::Boolean(deferred_stop)},
                    {"fresh_session", diag::Boolean(fresh_session)},
                    {"resources_balanced", diag::Boolean(stats.ResourcesBalanced())},
                    {"frames_acquired", diag::Number(stats.frames_acquired)},
                    {"leases_returned", diag::Number(stats.leases_returned)},
                    {"buffers_released", diag::Number(stats.buffers_released)},
                    {"allocated", diag::Number(stats.allocated)},
                    {"destroyed", diag::Number(stats.destroyed)},
                    {"error", diag::Quote(error)}});
  if (!diag::WriteReport(argv[2], report)) return 2;
  std::cout << report << '\n';
  return passed ? 0 : 1;
}
