#include <cstddef>

#include "cockpit/modules/camera/isp/raw10_unpack_backend.h"

namespace cockpit::camera::detail {

void UnpackRaw10RowScalar(const std::uint8_t* source, std::uint16_t* destination,
                          std::uint32_t width) {
  for (std::uint32_t x = 0; x < width; ++x) {
    const std::size_t offset = static_cast<std::size_t>(x) * 2U;
    const std::uint16_t container = static_cast<std::uint16_t>(source[offset]) |
                                    static_cast<std::uint16_t>(source[offset + 1U]) << 8U;
    destination[x] = static_cast<std::uint16_t>(container >> 6U);
  }
}

}  // namespace cockpit::camera::detail
