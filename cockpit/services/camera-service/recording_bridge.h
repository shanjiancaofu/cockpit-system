#pragma once

#include <cstdint>

#include "cockpit/core/event/message_bus.h"

namespace cockpit {
namespace camera {

class CameraRecordingBridgeFilter {
 public:
  explicit CameraRecordingBridgeFilter(std::uint64_t frame_meta_sample_interval = 30);

  bool ShouldForward(const event::EventMessage& message);

 private:
  const std::uint64_t frame_meta_sample_interval_;
  std::uint64_t frame_meta_seen_ = 0;
};

}  // namespace camera
}  // namespace cockpit
