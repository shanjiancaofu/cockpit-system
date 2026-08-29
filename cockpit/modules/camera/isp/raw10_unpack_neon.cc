#include <arm_neon.h>

#include <cstddef>

#include "cockpit/modules/camera/isp/raw10_unpack_backend.h"

namespace cockpit::camera::detail {

bool Raw10NeonAvailable() {
  return true;
}

void UnpackRaw10RowNeon(const std::uint8_t* source, std::uint16_t* destination,
                        std::uint32_t width) {
  std::uint32_t x = 0;
  for (; x + 8U <= width; x += 8U) {
    const auto samples = vld1q_u16(reinterpret_cast<const std::uint16_t*>(source) + x);
    vst1q_u16(destination + x, vshrq_n_u16(samples, 6));
  }
  if (x < width) {
    UnpackRaw10RowScalar(source + static_cast<std::size_t>(x) * 2U, destination + x, width - x);
  }
}

}  // namespace cockpit::camera::detail
