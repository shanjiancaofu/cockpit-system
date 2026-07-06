#include "cockpit/modules/camera/frames/latest_frame_buffer.h"

#include <utility>

namespace cockpit {
namespace camera {

bool LatestFrameBuffer::Publish(CameraFrame frame) {
  if (!frame.IsValid()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (latest_frame_.has_value()) {
    ++frames_replaced_;
  }
  latest_frame_ = std::move(frame);
  ++frames_published_;
  ++generation_;
  return true;
}

bool LatestFrameBuffer::ReadLatest(CameraFrame* frame, std::uint64_t* generation) const {
  if (frame == nullptr) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (!latest_frame_.has_value()) {
    return false;
  }
  *frame = *latest_frame_;
  if (generation != nullptr) {
    *generation = generation_;
  }
  return true;
}

LatestFrameBufferStatus LatestFrameBuffer::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  LatestFrameBufferStatus result;
  result.frames_published = frames_published_;
  result.frames_replaced = frames_replaced_;
  result.generation = generation_;
  result.has_frame = latest_frame_.has_value();
  return result;
}

void LatestFrameBuffer::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  latest_frame_.reset();
}

}  // namespace camera
}  // namespace cockpit
