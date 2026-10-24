#pragma once

#include <cstddef>
#include <cstdint>

namespace cockpit::camera {

void UnpackRaw10(const std::uint8_t* source, std::uint32_t width, std::uint32_t height,
                 std::uint32_t source_stride_bytes, std::uint16_t* destination,
                 std::size_t destination_stride_elements);

}  // namespace cockpit::camera
