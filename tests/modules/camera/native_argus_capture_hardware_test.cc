#include <fcntl.h>

#include <chrono>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

#include "cockpit/driver/camera/capture/native_argus_capture.h"
#include "tools/camera/camera-diag/diagnostics.h"

namespace camera = cockpit::camera;
namespace diag = cockpit::camera_diag;
namespace {
void Require(bool ok, const std::string& message) {
  if (!ok) throw std::runtime_error(message);
}
void Acquire(camera::NativeArgusCapture& capture, camera::FrameLease* lease) {
  std::string error;
  const auto status = capture.Acquire(lease, 5000, &error);
  Require(status == camera::NativeAcquireStatus::kFrame, "Acquire: " + error);
}
}  // namespace

// Explicit hardware-only executable. Never registered as an automatic CTest.
int main(int argc, char** argv) {
  std::string output = "native_runtime_lifecycle.json";
  if (argc != 3 || std::string(argv[1]) != "--output") return 2;
  output = argv[2];
  std::string error;
  camera::NativeArgusCapture capture;
  camera::NativeCaptureConfig config;
  config.buffer_count = 3;
  bool held_stop = false, starvation = false, threaded_return = false;
  bool timeout_recovery = false, destructor_lifetime = false, restart = false;
  try {
    Require(capture.Start(config, &error), "Start: " + error);
    std::vector<camera::FrameLease> held;
    std::set<int> fds;
    for (unsigned i = 0; i < config.buffer_count; ++i) {
      camera::FrameLease lease;
      Acquire(capture, &lease);
      const auto& frame = lease.frame();
      Require(frame.sensor_timestamp_ns > 0 && frame.frame_duration_ns > 0 &&
                  frame.received_at_ns > 0 && frame.received_steady_ns > 0,
              "metadata missing");
      Require(frame.source_clock == camera::CameraTimestampClock::kUnknown, "sensor clock guessed");
      Require(fcntl(frame.dmabuf_fd, F_GETFD) >= 0 && fds.insert(frame.dmabuf_fd).second,
              "live FD reused");
      Require(capture.Acquire(&lease, 10, &error) == camera::NativeAcquireStatus::kError && lease,
              "Acquire overwrote live lease");
      std::uint64_t sum = 0;
      Require(capture.SampleLuma(lease, 64, &sum, &error), "sample: " + error);
      Require(!capture.SampleLuma(lease, frame.width + 1, &sum, &error),
              "unbounded sample accepted");
      held.push_back(std::move(lease));
    }
    camera::FrameLease extra;
    const auto started = std::chrono::steady_clock::now();
    starvation = capture.Acquire(&extra, 5000, &error) == camera::NativeAcquireStatus::kNoBuffer &&
                 std::chrono::steady_clock::now() - started < std::chrono::milliseconds(500);
    Require(starvation && !extra, "pool starvation was not bounded");
    held_stop = capture.Stop(&error) == camera::NativeStopStatus::kLeasesOutstanding &&
                !capture.running() && capture.stats().destroyed == 0;
    Require(held_stop, "Stop freed leased buffers");
    Require(!capture.Start(config, &error), "Start replaced held session");
    Require(capture.Acquire(&extra, 100, &error) == camera::NativeAcquireStatus::kStopped,
            "stopping session produced a new lease");
    for (int fd : fds) Require(fcntl(fd, F_GETFD) >= 0, "held FD was closed");
    std::thread consumer([owned = std::move(held)]() mutable {
      owned.clear();
    });
    consumer.join();
    const auto pending = capture.stats();
    threaded_return = pending.outstanding_leases == 0 &&
                      pending.pending_returns == config.buffer_count && pending.destroyed == 0;
    Require(threaded_return, "consumer return invoked premature destruction");
    Require(capture.Stop(&error) == camera::NativeStopStatus::kStopped &&
                capture.stats().ResourcesBalanced(),
            "final Stop: " + error);
    Require(capture.Stop(&error) == camera::NativeStopStatus::kStopped, "Stop not idempotent");

    Require(capture.Start(config, &error), "restart: " + error);
    camera::FrameLease recovered;
    Require(capture.Acquire(&recovered, 1, &error) == camera::NativeAcquireStatus::kTimeout &&
                !recovered && capture.running(),
            "timeout did not leave a usable session");
    Acquire(capture, &recovered);
    recovered.Reset();
    Require(capture.Stop(&error) == camera::NativeStopStatus::kStopped,
            "stop after timeout: " + error);
    timeout_recovery = capture.stats().ResourcesBalanced();
    Require(timeout_recovery, "timeout recovery leaked resources");
    Require(capture.Start(config, &error), "second restart: " + error);
    Acquire(capture, &recovered);
    recovered.Reset();
    restart = capture.Stop(&error) == camera::NativeStopStatus::kStopped &&
              capture.stats().ResourcesBalanced();
    Require(restart, "same runtime restart failed");

    camera::FrameLease survivor;
    int borrowed = -1;
    {
      camera::NativeArgusCapture temporary;
      Require(temporary.Start(config, &error), "temporary Start: " + error);
      Acquire(temporary, &survivor);
      borrowed = survivor.frame().dmabuf_fd;
    }
    Require(survivor && fcntl(borrowed, F_GETFD) >= 0,
            "runtime destruction invalidated a held frame");
    std::thread final_consumer([owned = std::move(survivor)]() mutable {
      owned.Reset();
    });
    final_consumer.join();
    destructor_lifetime = fcntl(borrowed, F_GETFD) < 0;
    Require(destructor_lifetime, "last lease did not release session storage");
    Require(capture.Start(config, &error), "reopen after deferred destruction: " + error);
    Acquire(capture, &recovered);
    recovered.Reset();
    Require(capture.Stop(&error) == camera::NativeStopStatus::kStopped &&
                capture.stats().ResourcesBalanced(),
            "deferred destruction left camera unusable");
    error.clear();
  } catch (const std::exception& exception) {
    error = exception.what();
  }
  std::string stop_error;
  capture.Stop(&stop_error);
  const auto stats = capture.stats();
  const bool passed = error.empty() && held_stop && starvation && threaded_return &&
                      timeout_recovery && restart && destructor_lifetime &&
                      stats.ResourcesBalanced();
  const auto report =
      diag::Object({{"schema_version", "1"},
                    {"passed", diag::Boolean(passed)},
                    {"held_lease_stop", diag::Boolean(held_stop)},
                    {"pool_starvation", diag::Boolean(starvation)},
                    {"cross_thread_return", diag::Boolean(threaded_return)},
                    {"same_session_timeout_recovery", diag::Boolean(timeout_recovery)},
                    {"same_runtime_restart", diag::Boolean(restart)},
                    {"lease_outlives_runtime", diag::Boolean(destructor_lifetime)},
                    {"allocated", diag::Number(stats.allocated)},
                    {"destroyed", diag::Number(stats.destroyed)},
                    {"frames_acquired", diag::Number(stats.frames_acquired)},
                    {"leases_returned", diag::Number(stats.leases_returned)},
                    {"buffers_released", diag::Number(stats.buffers_released)},
                    {"outstanding_leases", diag::Number(stats.outstanding_leases)},
                    {"resources_balanced", diag::Boolean(stats.ResourcesBalanced())},
                    {"cleanup_errors", diag::Strings(stats.cleanup_errors)},
                    {"error", diag::Quote(error)}});
  if (!diag::WriteReport(output, report)) return 2;
  std::cout << "passed=" << passed << " report=" << output << " error=" << error << '\n';
  return passed ? 0 : 1;
}
