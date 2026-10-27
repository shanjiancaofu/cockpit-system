#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "cockpit/driver/camera/capture/camera_preview_source.h"
#include "cockpit/driver/camera/capture/native_argus_capture.h"
#include "cockpit/driver/camera/frames/frame_broker.h"
#include "cockpit/driver/camera/transform/vic_transform.h"

namespace cockpit::camera {
struct NativePreviewStatus {
  bool running = false;
  std::uint64_t session_id = 0, frames_delivered = 0;
  std::uint32_t last_delivered_capture_id = 0;
  CameraTuningMetadata last_delivered_tuning;
  double delivery_p95_ms = 0;
  std::uint64_t latency_samples = 0;
  NativeCaptureStats capture;
  FrameBrokerStats broker;
  VicTransformStats vic;
  std::string error;
};

// One capture worker plus one latest-only VIC/delivery worker. Hardware leases
// are relinquished before invoking the existing CPU CameraFrame callback.
class NativeArgusPreviewSource final : public CameraPreviewSource {
 public:
  NativeArgusPreviewSource();
  ~NativeArgusPreviewSource() override;
  bool Start(const CameraPreviewConfig&, FrameCallback, std::string*) override;
  void Stop() override;
  bool IsRunning() const override;
  bool GetTuning(CameraTuningState*, std::string*) const override;
  bool SetTuning(std::uint64_t, std::uint32_t, const CameraTuning&, CameraTuningState*,
                 std::string*) override;
  bool ResetTuning(std::uint64_t, std::uint32_t, CameraTuningState*, std::string*) override;
  bool ApplyTuningProfile(std::uint64_t, std::uint32_t, const TuningProfile&, CameraTuningState*,
                          std::string*) override;
  bool GetNativeStatus(NativePreviewStatus*) const override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace cockpit::camera
