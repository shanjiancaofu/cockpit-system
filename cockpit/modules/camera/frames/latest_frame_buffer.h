#pragma once

#include <cstdint>
#include <mutex>
#include <optional>

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

  LatestFrameBuffer(const LatestFrameBuffer&) = delete;
  LatestFrameBuffer& operator=(const LatestFrameBuffer&) = delete;

  bool Publish(CameraFrame frame) override;
  bool ReadLatest(CameraFrame* frame, std::uint64_t* generation = nullptr) const;
  LatestFrameBufferStatus status() const;
  void Clear();

 private:
  mutable std::mutex mutex_;
  std::optional<CameraFrame> latest_frame_;
  std::uint64_t frames_published_ = 0;
  std::uint64_t frames_replaced_ = 0;
  std::uint64_t generation_ = 0;
};

}  // namespace camera
}  // namespace cockpit
