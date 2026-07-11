#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "cockpit/core/base/macros.h"
#include "cockpit/modules/camera/capture/camera_preview_source.h"

namespace cockpit {
namespace camera {

enum class SyntheticCameraFault {
  kNone,
  kNoFrames,
  kStall,
  kDisconnect,
};

struct SyntheticCameraOptions {
  SyntheticCameraFault fault = SyntheticCameraFault::kNone;
  std::uint64_t fault_after_frames = 30;
};

class SyntheticPreviewSource final : public CameraPreviewSource {
 public:
  explicit SyntheticPreviewSource(SyntheticCameraOptions options = {});
  ~SyntheticPreviewSource() override;

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(SyntheticPreviewSource);

  bool Start(const CameraPreviewConfig& config, FrameCallback callback,
             std::string* error) override;
  void Stop() override;
  bool IsRunning() const override;

  void SetFault(SyntheticCameraFault fault, std::uint64_t fault_after_frames);

 private:
  void Run();

  mutable std::mutex mutex_;
  CameraPreviewConfig config_;
  FrameCallback callback_;
  std::thread worker_;
  std::atomic_bool stop_requested_{false};
  std::atomic_bool running_{false};
  std::atomic<SyntheticCameraFault> fault_{SyntheticCameraFault::kNone};
  std::atomic<std::uint64_t> fault_after_frames_{30};
};

SyntheticCameraFault ParseSyntheticCameraFault(const std::string& value);
const char* ToString(SyntheticCameraFault fault);

}  // namespace camera
}  // namespace cockpit
