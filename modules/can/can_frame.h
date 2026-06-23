#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace cockpit {
namespace can {

class CanFrame {
 public:
  static constexpr std::size_t kMaxDataLength = 8;

  CanFrame() = default;
  CanFrame(std::uint32_t id, const std::array<std::uint8_t, kMaxDataLength>& data,
           std::uint8_t data_length, bool extended = false, bool remote = false);

  std::uint32_t id() const {
    return id_;
  }
  const std::array<std::uint8_t, kMaxDataLength>& data() const {
    return data_;
  }
  std::uint8_t data_length() const {
    return data_length_;
  }
  bool extended() const {
    return extended_;
  }
  bool remote() const {
    return remote_;
  }

  bool IsValid() const;
  std::string ToString() const;

 private:
  std::uint32_t id_ = 0;
  std::array<std::uint8_t, kMaxDataLength> data_{};
  std::uint8_t data_length_ = 0;
  bool extended_ = false;
  bool remote_ = false;
};

}  // namespace can
}  // namespace cockpit
