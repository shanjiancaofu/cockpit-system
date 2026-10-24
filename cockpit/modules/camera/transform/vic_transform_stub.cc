#include "cockpit/modules/camera/transform/vic_transform.h"
namespace cockpit::camera {
struct VicTransform::Impl {};
VicTransform::VicTransform() : impl_(std::make_unique<Impl>()) {
}
VicTransform::~VicTransform() = default;
bool VicTransform::IsSupported() {
  return false;
}
bool VicTransform::Convert(SharedFrameLease, const VicTransformConfig&, CpuVideoFrame*,
                           std::string* error) {
  if (error) *error = "VIC SDK unavailable or disabled; no transform performed";
  return false;
}
bool VicTransform::Reset(std::string*) {
  return true;
}
VicTransformStats VicTransform::stats() const {
  return {};
}
}  // namespace cockpit::camera
