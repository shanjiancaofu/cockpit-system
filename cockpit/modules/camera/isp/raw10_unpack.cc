#include "cockpit/modules/camera/isp/raw10_unpack.h"

#include <cstdlib>

#include "cockpit/modules/camera/isp/raw10_unpack_backend.h"

namespace cockpit::camera {

void UnpackRaw10(const std::uint8_t* source, std::uint32_t width, std::uint32_t height,
                 std::uint32_t source_stride_bytes, std::uint16_t* destination,
                 std::size_t destination_stride_elements) {
  const bool use_neon =
      detail::Raw10NeonAvailable() && std::getenv("COCKPIT_CAMERA_DISABLE_NEON") == nullptr;
  for (std::uint32_t y = 0; y < height; ++y) {
    const auto* source_row = source + static_cast<std::size_t>(y) * source_stride_bytes;
    auto* destination_row = destination + static_cast<std::size_t>(y) * destination_stride_elements;
    if (use_neon) {
      detail::UnpackRaw10RowNeon(source_row, destination_row, width);
    } else {
      detail::UnpackRaw10RowScalar(source_row, destination_row, width);
    }
  }
}

}  // namespace cockpit::camera
