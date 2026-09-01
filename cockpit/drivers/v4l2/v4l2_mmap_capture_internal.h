#pragma once

#include <linux/videodev2.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "cockpit/drivers/v4l2/v4l2_mmap_capture.h"

namespace cockpit::camera::detail {

enum class DequeuedBufferResult {
  kSuccess,
  kInvalidIndex,
  kInvalidFrame,
  kCopyFailed,
  kRequeueFailed,
};

using RequeueBuffer = std::function<int(v4l2_buffer*)>;

DequeuedBufferResult CopyAndRequeueDequeuedBuffer(const std::vector<V4l2MappedBuffer>& buffers,
                                                  std::uint32_t width, std::uint32_t height,
                                                  std::uint32_t bytes_per_line, v4l2_buffer* buffer,
                                                  V4l2RawFrame* frame, const RequeueBuffer& requeue,
                                                  std::string* error);

}  // namespace cockpit::camera::detail
