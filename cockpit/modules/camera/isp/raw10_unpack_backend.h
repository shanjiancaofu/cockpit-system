#pragma once

#include <cstdint>

namespace cockpit::camera::detail {

void UnpackRaw10RowScalar(const std::uint8_t* source, std::uint16_t* destination,
                          std::uint32_t width);
bool Raw10NeonAvailable();
void UnpackRaw10RowNeon(const std::uint8_t* source, std::uint16_t* destination,
                        std::uint32_t width);

}  // namespace cockpit::camera::detail
