#include "cockpit/modules/camera/transform/vic_transform.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

int main() {
  using namespace cockpit::camera;
  using detail::CopyMappedVideoPlanes;
  std::string error;
  std::vector<std::uint8_t> y(32, 250), uv(16, 240);
  for (int row = 0; row < 4; ++row)
    for (int x = 0; x < 4; ++x) y[row * 8 + x] = row * 4 + x;
  for (int row = 0; row < 2; ++row)
    for (int x = 0; x < 4; ++x) uv[row * 8 + x] = 100 + row * 4 + x;
  CpuVideoFrame frame;
  assert(CopyMappedVideoPlanes({{y.data(), y.size(), 8}, {uv.data(), uv.size(), 8}},
                               {4, 4, VicOutputFormat::kNv12}, &frame, &error));
  assert(frame.planes.size() == 2 && frame.planes[1].offset == 32 && frame.data.size() == 48);
  assert(frame.data[0] == 0 && frame.data[11] == 7 && frame.data[43] == 107);
  assert(frame.data[4] == 0 && frame.data[44] == 0);  // No exported allocator padding.
  const auto previous = frame.data;
  assert(!CopyMappedVideoPlanes({{y.data(), 27, 8}, {uv.data(), uv.size(), 8}},
                                {4, 4, VicOutputFormat::kNv12}, &frame, &error));
  assert(frame.data == previous);
  assert(!CopyMappedVideoPlanes({}, {3, 4, VicOutputFormat::kNv12}, &frame, &error));
  assert(!CopyMappedVideoPlanes({{y.data(), y.size(), 2}}, {4, 4, VicOutputFormat::kBgrx}, &frame,
                                &error));
  assert(!CopyMappedVideoPlanes({{y.data(), SIZE_MAX, UINT32_MAX}},
                                {8192, 8192, VicOutputFormat::kBgrx}, &frame, &error));
  std::vector<std::uint8_t> u(8, 90), v(8, 240);
  assert(CopyMappedVideoPlanes({{y.data(), y.size(), 8}, {u.data(), 8, 4}, {v.data(), 8, 4}},
                               {4, 4, VicOutputFormat::kI420}, &frame, &error));
  assert(frame.planes.size() == 3 && frame.planes[1].offset == 32 && frame.planes[2].offset == 40);
  assert(frame.data[32] == 90 && frame.data[40] == 240);
  std::vector<std::uint8_t> bgrx(48, 7);
  assert(CopyMappedVideoPlanes({{bgrx.data(), bgrx.size(), 24}}, {4, 2, VicOutputFormat::kBgrx},
                               &frame, &error));
  frame.capture_id = 123;
  frame.sensor_timestamp_ns = 100;
  frame.received_at_ns = 1000000000;
  CameraFrame legacy;
  assert(TakeCameraFrame(std::move(frame), &legacy, &error));
  assert(legacy.IsValid() && legacy.stride_bytes == 24 && legacy.data[16] == 0);
  assert(legacy.sequence == 123 && legacy.timestamp_ms == 1000 &&
         legacy.source_clock == CameraTimestampClock::kUnknown);
  VicTransform converter;
  CpuVideoFrame untouched;
  untouched.width = 99;
  assert(!converter.Convert({}, {}, &untouched, &error) && untouched.width == 99);
  assert(converter.Reset() && converter.stats().ResourcesBalanced());
  return 0;
}
