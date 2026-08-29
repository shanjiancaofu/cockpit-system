#include "cockpit/modules/camera/isp/raw10_unpack.h"

#include <cstddef>
#include <cstdlib>

namespace cockpit::camera {
namespace detail {

void UnpackRaw10RowNeon(const std::uint8_t* source, std::uint16_t* destination,
                        std::uint32_t width);

}  // namespace detail
namespace {

using RowUnpacker = void (*)(const std::uint8_t*, std::uint16_t*, std::uint32_t);

void UnpackRaw10RowScalar(const std::uint8_t* source, std::uint16_t* destination,
                          std::uint32_t width) {
  for (std::uint32_t x = 0; x < width; ++x) {
    const std::size_t offset = static_cast<std::size_t>(x) * 2U;
    const std::uint16_t container = static_cast<std::uint16_t>(source[offset]) |
                                    static_cast<std::uint16_t>(source[offset + 1U]) << 8U;
    destination[x] = static_cast<std::uint16_t>(container >> 6U);
  }
}

RowUnpacker SelectRowUnpacker() {
#if defined(COCKPIT_CAMERA_HAS_NEON)
  if (std::getenv("COCKPIT_CAMERA_DISABLE_NEON") == nullptr) {
    return detail::UnpackRaw10RowNeon;
  }
#endif
  return UnpackRaw10RowScalar;
}

}  // namespace

void UnpackRaw10(const std::uint8_t* source, std::uint32_t width, std::uint32_t height,
                 std::uint32_t source_stride_bytes, std::uint16_t* destination,
                 std::size_t destination_stride_elements) {
  const RowUnpacker unpack_row = SelectRowUnpacker();
  for (std::uint32_t y = 0; y < height; ++y) {
    const auto* source_row = source + static_cast<std::size_t>(y) * source_stride_bytes;
    auto* destination_row = destination + static_cast<std::size_t>(y) * destination_stride_elements;
    unpack_row(source_row, destination_row, width);
  }
}

}  // namespace cockpit::camera
