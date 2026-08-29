#include "cockpit/modules/camera/isp/raw10_unpack_backend.h"

namespace cockpit::camera::detail {

bool Raw10NeonAvailable() {
  return false;
}

void UnpackRaw10RowNeon(const std::uint8_t* source, std::uint16_t* destination,
                        std::uint32_t width) {
  UnpackRaw10RowScalar(source, destination, width);
}

}  // namespace cockpit::camera::detail
