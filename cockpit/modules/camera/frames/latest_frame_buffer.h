#pragma once

#include <cstdint>
#include <mutex>
#include <optional>

#include "cockpit/core/base/macros.h"
#include "cockpit/modules/camera/frames/camera_frame_sink.h"

namespace cockpit {
namespace camera {

struct LatestFrameBufferStatus {
  std::uint64_t frames_published = 0;
  std::uint64_t frames_replaced = 0;
  std::uint64_t generation = 0;
  bool has_frame = false;
};

class LatestFrameBuffer final : public CameraFrameSink {
 public:
  LatestFrameBuffer() = default;

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(LatestFrameBuffer);

  bool Publish(CameraFrame frame) override;
  bool PublishHandle(CameraFrameHandle frame);
  CameraFrameHandle LatestHandle(std::uint64_t* generation = nullptr) const;
  bool ReadLatest(CameraFrame* frame, std::uint64_t* generation = nullptr) const;
  LatestFrameBufferStatus status() const;
  void Clear();

 private:
  mutable std::mutex mutex_;
  CameraFrameHandle latest_frame_;
  std::uint64_t frames_published_ = 0;
  std::uint64_t frames_replaced_ = 0;
  std::uint64_t generation_ = 0;
};

}  // namespace camera
}  // namespace cockpit
