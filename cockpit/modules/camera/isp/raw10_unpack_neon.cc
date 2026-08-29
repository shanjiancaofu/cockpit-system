#include <arm_neon.h>

#include <cstddef>

#include "cockpit/modules/camera/isp/raw10_unpack.h"

namespace cockpit::camera::detail {

void UnpackRaw10RowNeon(const std::uint8_t* source, std::uint16_t* destination,
                        std::uint32_t width) {
  std::uint32_t x = 0;
  for (; x + 8U <= width; x += 8U) {
    const auto samples = vld1q_u16(reinterpret_cast<const std::uint16_t*>(source) + x);
    vst1q_u16(destination + x, vshrq_n_u16(samples, 6));
  }
  for (; x < width; ++x) {
    const std::size_t offset = static_cast<std::size_t>(x) * 2U;
    const std::uint16_t container = static_cast<std::uint16_t>(source[offset]) |
                                    static_cast<std::uint16_t>(source[offset + 1U]) << 8U;
    destination[x] = static_cast<std::uint16_t>(container >> 6U);
  }
}

}  // namespace cockpit::camera::detail
