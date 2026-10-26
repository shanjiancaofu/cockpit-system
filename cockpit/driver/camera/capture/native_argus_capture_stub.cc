#include "cockpit/driver/camera/capture/native_argus_capture.h"

namespace cockpit::camera {
struct NativeArgusCapture::Impl {};
NativeArgusCapture::NativeArgusCapture() : impl_(std::make_unique<Impl>()) {
}
NativeArgusCapture::~NativeArgusCapture() = default;
bool NativeArgusCapture::IsSupported() {
  return false;
}
bool NativeArgusCapture::Start(const NativeCaptureConfig&, std::string* error) {
  if (error) *error = "native Argus SDK unavailable or disabled at build time";
  return false;
}
NativeAcquireStatus NativeArgusCapture::Acquire(FrameLease* lease, int timeout_ms,
                                                std::string* error) {
  if (!lease || *lease || timeout_ms <= 0) {
    if (error) *error = "Acquire requires an empty lease and positive timeout";
    return NativeAcquireStatus::kError;
  }
  return NativeAcquireStatus::kStopped;
}
NativeStopStatus NativeArgusCapture::Stop(std::string*) {
  return NativeStopStatus::kStopped;
}
bool NativeArgusCapture::running() const {
  return false;
}
NativeCaptureStats NativeArgusCapture::stats() const {
  return {};
}
std::string NativeArgusCapture::version() const {
  return {};
}
int NativeArgusCapture::sensor_mode() const {
  return -1;
}
bool NativeArgusCapture::GetTuning(CameraTuningState*, std::string* error) const {
  if (error) *error = "native Argus SDK unavailable or disabled at build time";
  return false;
}
bool NativeArgusCapture::SetTuning(const CameraTuning&, std::uint32_t*, std::string* error) {
  if (error) *error = "native Argus SDK unavailable or disabled at build time";
  return false;
}
bool NativeArgusCapture::ResetTuning(std::uint32_t*, std::string* error) {
  if (error) *error = "native Argus SDK unavailable or disabled at build time";
  return false;
}
bool NativeArgusCapture::ApplyTuningProfile(const TuningProfile&, std::uint32_t*,
                                            std::string* error) {
  if (error) *error = "native Argus SDK unavailable or disabled at build time";
  return false;
}
bool NativeArgusCapture::SampleLuma(const FrameLease&, std::size_t, std::uint64_t*,
                                    std::string* error) {
  if (error) *error = "native Argus SDK unavailable or disabled at build time";
  return false;
}
}  // namespace cockpit::camera
