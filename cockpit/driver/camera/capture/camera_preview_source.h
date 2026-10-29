#pragma once

#include <functional>
#include <string>

#include "cockpit/driver/camera/frames/camera_frame.h"
#include "cockpit/driver/camera/tuning/camera_tuning.h"

namespace cockpit {
namespace camera {

struct NativePreviewStatus;
struct TuningProfile;

enum class CameraCapturePipeline {
  kArgusIsp,
  kNativeArgus,
  kUvc,
  kSoftwareIsp,
  kSynthetic,
};

enum class CameraUvcInputFormat {
  kMjpeg,
  kYuyv,
};

struct CameraPreviewConfig {
  std::string device = "/dev/video0";
  std::uint32_t width = 1920;
  std::uint32_t height = 1080;
  std::uint32_t fps = 30;
  CameraPixelFormat output_format = CameraPixelFormat::kBgrx;
  std::string tuning_profile;
  std::string h264_output_path;
  std::uint32_t h264_width = 1280;
  std::uint32_t h264_height = 720;
  std::uint32_t h264_bitrate = 2'000'000;
  std::uint32_t h264_gop = 30;
  std::string h264_udp_host;
  std::uint16_t h264_udp_port = 0;
};

class CameraPreviewSource {
 public:
  using FrameCallback = std::function<void(CameraFrame)>;

  virtual ~CameraPreviewSource() = default;

  virtual bool Start(const CameraPreviewConfig& config, FrameCallback callback,
                     std::string* error) = 0;
  virtual void Stop() = 0;
  virtual bool IsRunning() const = 0;
  virtual bool GetTuning(CameraTuningState*, std::string* error) const {
    return Unsupported(error);
  }
  virtual bool SetTuning(std::uint64_t, std::uint32_t, const CameraTuning&, CameraTuningState*,
                         std::string* error) {
    return Unsupported(error);
  }
  virtual bool ResetTuning(std::uint64_t, std::uint32_t, CameraTuningState*, std::string* error) {
    return Unsupported(error);
  }
  virtual bool ApplyTuningProfile(std::uint64_t, std::uint32_t, const TuningProfile&,
                                  CameraTuningState*, std::string* error) {
    return Unsupported(error);
  }
  virtual bool GetNativeStatus(NativePreviewStatus*) const {
    return false;
  }
  static bool Unsupported(std::string* error) {
    if (error) *error = "ISP controls require native_argus backend";
    return false;
  }
  virtual bool IsRecovering() const {
    return false;
  }
};

}  // namespace camera
}  // namespace cockpit
