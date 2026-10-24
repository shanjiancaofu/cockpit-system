#include <cstring>
#include <limits>
#include <utility>

#include "cockpit/modules/camera/transform/vic_transform.h"

namespace cockpit::camera {
namespace detail {
bool CopyMappedVideoPlanes(const std::vector<MappedVideoPlane>& planes,
                           const VicTransformConfig& config, CpuVideoFrame* output,
                           std::string* error) {
  const auto fail = [&](const char* reason) {
    if (error) *error = reason;
    return false;
  };
  const auto w = config.width, h = config.height;
  if (!output || !w || !h || w > 8192 || h > 8192) return fail("invalid CPU output dimensions");
  std::vector<std::uint32_t> rows, heights;
  switch (config.format) {
    case VicOutputFormat::kBgrx:
      rows = {w * 4};
      heights = {h};
      break;
    case VicOutputFormat::kNv12:
      rows = {w, w};
      heights = {h, h / 2};
      break;
    case VicOutputFormat::kI420:
      rows = {w, w / 2, w / 2};
      heights = {h, h / 2, h / 2};
      break;
    default:
      return fail("unsupported CPU output format");
  }
  if (config.format != VicOutputFormat::kBgrx && (w % 2 || h % 2))
    return fail("4:2:0 output requires even dimensions");
  if (planes.size() != rows.size()) return fail("unexpected plane count");
  CpuVideoFrame next;
  next.width = w;
  next.height = h;
  next.format = config.format;
  std::size_t total = 0;
  for (std::size_t p = 0; p < planes.size(); ++p) {
    const auto& plane = planes[p];
    if (!plane.data || plane.stride < rows[p] ||
        static_cast<std::uint64_t>(heights[p] - 1) * plane.stride + rows[p] > plane.size)
      return fail("invalid plane pointer, pitch or mapped size");
    const std::uint64_t bytes = static_cast<std::uint64_t>(plane.stride) * heights[p];
    if (bytes > 256 * 1024 * 1024 || total + bytes > 256 * 1024 * 1024)
      return fail("CPU output exceeds byte limit");
    next.planes.push_back({total, plane.stride, rows[p], heights[p]});
    total += bytes;
  }
  next.data.resize(total, 0);  // Never export uninitialized padding.
  for (std::size_t p = 0; p < planes.size(); ++p) {
    const auto& layout = next.planes[p];
    for (std::uint32_t row = 0; row < layout.height; ++row)
      std::memcpy(next.data.data() + layout.offset + static_cast<std::size_t>(row) * layout.stride,
                  planes[p].data + static_cast<std::size_t>(row) * planes[p].stride,
                  layout.row_bytes);
  }
  *output = std::move(next);
  return true;
}
}  // namespace detail
bool TakeCameraFrame(CpuVideoFrame&& input, CameraFrame* output, std::string* error) {
  const auto fail = [&] {
    if (error) *error = "expected owned BGRx plane for CameraFrame";
    return false;
  };
  if (!output || input.format != VicOutputFormat::kBgrx || input.planes.size() != 1 ||
      input.planes[0].offset != 0 ||
      input.planes[0].row_bytes != static_cast<std::uint64_t>(input.width) * 4 ||
      input.planes[0].height != input.height ||
      input.sensor_timestamp_ns > static_cast<std::uint64_t>(INT64_MAX))
    return fail();
  CameraFrame next;
  next.sequence = input.capture_id;
  next.width = input.width;
  next.height = input.height;
  next.stride_bytes = input.planes[0].stride;
  next.format = CameraPixelFormat::kBgrx;
  next.received_at_ns = input.received_at_ns;
  next.timestamp_ms = input.received_at_ns > 0 ? input.received_at_ns / 1000000 : 0;
  next.source_timestamp_ns = static_cast<std::int64_t>(input.sensor_timestamp_ns);
  next.source_timestamp_valid = input.sensor_timestamp_ns > 0;
  next.source_clock = input.source_clock;
  if (!next.HasValidLayout() || !next.HasValidPayloadSize(input.data.size())) return fail();
  next.data = std::move(input.data);
  *output = std::move(next);
  return true;
}
}  // namespace cockpit::camera
