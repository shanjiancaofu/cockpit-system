#include <cassert>
#include <cerrno>
#include <cstdint>
#include <string>
#include <vector>

#include "cockpit/drivers/v4l2/v4l2_mmap_capture_internal.h"

int main() {
  using cockpit::camera::V4l2RawFrame;
  using cockpit::camera::detail::CopyAndRequeueDequeuedBuffer;
  using cockpit::camera::detail::DequeuedBufferResult;
  using cockpit::camera::detail::V4l2MappedBuffer;

  std::vector<std::uint8_t> storage{1, 2, 3, 4};
  const std::vector<V4l2MappedBuffer> buffers{{storage.data(), storage.size()}};
  V4l2RawFrame frame;
  std::string error;
  int requeue_count = 0;
  v4l2_buffer buffer{};
  buffer.index = 0;
  buffer.bytesused = storage.size();
  buffer.sequence = 7;

  auto requeue = [&requeue_count](v4l2_buffer*) {
    ++requeue_count;
    return 0;
  };
  assert(CopyAndRequeueDequeuedBuffer(buffers, 2, 2, 2, &buffer, &frame, requeue, &error) ==
         DequeuedBufferResult::kSuccess);
  assert(requeue_count == 1);
  assert(frame.data == storage);

  buffer.index = 1;
  assert(CopyAndRequeueDequeuedBuffer(buffers, 2, 2, 2, &buffer, &frame, requeue, &error) ==
         DequeuedBufferResult::kInvalidIndex);
  assert(requeue_count == 1);

  buffer.index = 0;
  buffer.bytesused = storage.size() + 1;
  assert(CopyAndRequeueDequeuedBuffer(buffers, 2, 2, 2, &buffer, &frame, requeue, &error) ==
         DequeuedBufferResult::kInvalidFrame);
  assert(requeue_count == 2);

  buffer.bytesused = storage.size();
  auto failed_requeue = [&requeue_count](v4l2_buffer*) {
    ++requeue_count;
    errno = EIO;
    return -1;
  };
  assert(CopyAndRequeueDequeuedBuffer(buffers, 2, 2, 2, &buffer, &frame, failed_requeue, &error) ==
         DequeuedBufferResult::kRequeueFailed);
  assert(requeue_count == 3);
  assert(error.find("VIDIOC_QBUF") != std::string::npos);
  return 0;
}
