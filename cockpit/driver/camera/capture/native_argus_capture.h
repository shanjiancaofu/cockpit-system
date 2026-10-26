#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "cockpit/driver/camera/frames/native_frame.h"

namespace cockpit::camera {
struct TuningProfile;
struct NativeCaptureConfig {
  std::uint32_t device_index = 0, width = 1920, height = 1080, fps = 30;
  std::uint32_t buffer_count = 6;
  int shutdown_timeout_ms = 5000;
};
enum class NativeAcquireStatus { kFrame, kTimeout, kNoBuffer, kStopped, kError };
enum class NativeStopStatus { kStopped, kLeasesOutstanding, kError };

struct NativeCaptureStats {
  std::uint64_t starts = 0, frames_acquired = 0, leases_returned = 0, buffers_released = 0;
  std::uint64_t drained = 0, timeouts = 0, no_buffer = 0, release_errors = 0;
  std::uint64_t allocated = 0, destroyed = 0, imported_fds = 0;
  std::uint64_t egl_mapped = 0, egl_unmapped = 0, cpu_mapped = 0, cpu_unmapped = 0;
  std::uint64_t outstanding_leases = 0, pending_returns = 0, peak_leases = 0;
  bool native_resources_retained_until_exit = false;
  std::vector<std::string> cleanup_errors;
  bool ResourcesBalanced() const {
    return allocated == destroyed && egl_mapped == egl_unmapped && cpu_mapped == cpu_unmapped &&
           frames_acquired == leases_returned && leases_returned == buffers_released &&
           outstanding_leases == 0 && pending_returns == 0 && release_errors == 0 &&
           cleanup_errors.empty() && !native_resources_retained_until_exit;
  }
};

// Synchronous capture building block; no worker, broker or supervisor is created.
// Control calls are serialized. A FrameLease may be returned on another thread;
// returns are queued and flushed by Acquire/Stop while the runtime is alive.
// Stop returns kLeasesOutstanding until all consumers have relinquished their frames.
// Destruction requests stop; leases defer final drain and storage destruction.
// The final deferred shutdown can block the last releasing thread after runtime destruction.
class NativeArgusCapture final {
 public:
  NativeArgusCapture();
  ~NativeArgusCapture();
  NativeArgusCapture(const NativeArgusCapture&) = delete;
  NativeArgusCapture& operator=(const NativeArgusCapture&) = delete;

  static bool IsSupported();
  bool Start(const NativeCaptureConfig& config, std::string* error = nullptr);
  NativeAcquireStatus Acquire(FrameLease* lease, int timeout_ms, std::string* error = nullptr);
  NativeStopStatus Stop(std::string* error = nullptr);
  bool running() const;
  NativeCaptureStats stats() const;
  std::string version() const;
  int sensor_mode() const;
  // Serialized with Start/Acquire/Stop. Updates build a fresh Request and only
  // commit after SDK acceptance. Success means queued, not yet observed on a frame.
  bool GetTuning(CameraTuningState* state, std::string* error = nullptr) const;
  bool SetTuning(const CameraTuning& settings, std::uint32_t* revision = nullptr,
                 std::string* error = nullptr);
  bool ResetTuning(std::uint32_t* revision = nullptr, std::string* error = nullptr);
  // Validate target/capabilities and submit under the same control lock.
  // Acceptance is asynchronous; observe session_id/revision in acquired frames.
  bool ApplyTuningProfile(const TuningProfile& profile, std::uint32_t* revision = nullptr,
                          std::string* error = nullptr);
  // Small synchronous CPU read for validation; maps/synchronizes/unmaps within the lease.
  bool SampleLuma(const FrameLease& lease, std::size_t bytes, std::uint64_t* sum,
                  std::string* error = nullptr);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace cockpit::camera
