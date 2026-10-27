#pragma once

#include <functional>
#include <memory>
#include <string>

#include "cockpit/driver/camera/capture/camera_preview_source.h"
#include "cockpit/driver/camera/frames/camera_frame.h"
#include "cockpit/exchange/foundation/base/macros.h"
#include "cockpit/exchange/foundation/runtime/module.h"

namespace cockpit {
namespace camera {

class CameraPreviewModule : public runtime::BasicModule {
 public:
  using FrameCallback = CameraPreviewSource::FrameCallback;

  explicit CameraPreviewModule(std::unique_ptr<CameraPreviewSource> preview_source);
  ~CameraPreviewModule() override = default;

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(CameraPreviewModule);

  bool available() const;
  bool is_running() const;
  bool is_recovering() const;
  const std::string& last_error() const;
  bool GetTuning(CameraTuningState*, std::string*) const;
  bool SetTuning(std::uint64_t, std::uint32_t, const CameraTuning&, CameraTuningState*,
                 std::string*);
  bool ResetTuning(std::uint64_t, std::uint32_t, CameraTuningState*, std::string*);
  bool ApplyTuningProfile(std::uint64_t, std::uint32_t, const TuningProfile&, CameraTuningState*,
                          std::string*);
  bool GetNativeStatus(NativePreviewStatus*) const;

  void Configure(CameraPreviewConfig config, FrameCallback callback);

 protected:
  bool OnStart() override;
  void OnStop() override;

 private:
  std::unique_ptr<CameraPreviewSource> preview_source_;
  CameraPreviewConfig config_;
  FrameCallback callback_;
  std::string last_error_;
};

}  // namespace camera
}  // namespace cockpit
