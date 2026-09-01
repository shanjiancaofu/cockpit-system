#include "cockpit/modules/camera/frames/latest_frame_buffer.h"

#include <utility>

namespace cockpit {
namespace camera {

bool LatestFrameBuffer::Publish(CameraFrame frame) {
  return PublishHandle(std::make_shared<const CameraFrame>(std::move(frame)));
}

bool LatestFrameBuffer::PublishHandle(CameraFrameHandle frame) {
  if (!frame || !frame->IsValid()) return false;

  std::lock_guard<std::mutex> lock(mutex_);
  if (latest_frame_) {
    ++frames_replaced_;
  }
  latest_frame_ = std::move(frame);
  ++frames_published_;
  ++generation_;
  return true;
}

CameraFrameHandle LatestFrameBuffer::LatestHandle(std::uint64_t* generation) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (generation != nullptr) *generation = generation_;
  return latest_frame_;
}

bool LatestFrameBuffer::ReadLatest(CameraFrame* frame, std::uint64_t* generation) const {
  if (frame == nullptr) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (!latest_frame_) {
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
  result.has_frame = static_cast<bool>(latest_frame_);
  return result;
}

void LatestFrameBuffer::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  latest_frame_.reset();
}

}  // namespace camera
}  // namespace cockpit
