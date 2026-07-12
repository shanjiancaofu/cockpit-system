#include "cockpit/processes/camera/recording_bridge.h"

namespace cockpit {
namespace camera {

CameraRecordingBridgeFilter::CameraRecordingBridgeFilter(std::uint64_t frame_meta_sample_interval)
    : frame_meta_sample_interval_(frame_meta_sample_interval == 0 ? 1
                                                                  : frame_meta_sample_interval) {
}

bool CameraRecordingBridgeFilter::ShouldForward(const event::EventMessage& message) {
  if (message.topic.rfind("/camera/", 0) != 0) {
    return false;
  }
  if (message.topic != "/camera/frame_meta") {
    return true;
  }
  ++frame_meta_seen_;
  return frame_meta_seen_ % frame_meta_sample_interval_ == 1U;
}

}  // namespace camera
}  // namespace cockpit
