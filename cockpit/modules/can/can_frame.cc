#include "cockpit/modules/can/can_frame.h"

#include <iomanip>
#include <sstream>

namespace cockpit {
namespace can {
namespace {

constexpr std::uint32_t kStandardIdMask = 0x7FFU;
constexpr std::uint32_t kExtendedIdMask = 0x1FFFFFFFU;

}  // namespace

CanFrame::CanFrame(std::uint32_t id, const std::array<std::uint8_t, kMaxDataLength>& data,
                   std::uint8_t data_length, bool extended, bool remote, bool fd, bool brs,
                   bool esi)
    : id_(id),
      data_(data),
      data_length_(data_length),
      extended_(extended),
      remote_(remote),
      fd_(fd),
      brs_(brs),
      esi_(esi) {
}

bool CanFrame::IsValid() const {
  const std::uint32_t id_mask = extended_ ? kExtendedIdMask : kStandardIdMask;
  if (data_length_ > kMaxDataLength || (id_ & ~id_mask) != 0U) {
    return false;
  }
  if (fd_) {
    return !remote_;
  }
  return data_length_ <= 8U && !brs_ && !esi_;
}

std::string CanFrame::ToString() const {
  std::ostringstream out;
  out << std::uppercase << std::hex << id_ << '#';
  if (remote_) {
    out << 'R' << std::dec << static_cast<int>(data_length_);
    return out.str();
  }

  if (fd_) {
    out << '#';
    unsigned int flags = 0;
    if (brs_) {
      flags |= 1U;
    }
    if (esi_) {
      flags |= 2U;
    }
    out << flags;
  }

  for (std::size_t i = 0; i < data_length_; ++i) {
    out << std::setw(2) << std::setfill('0') << static_cast<int>(data_[i]);
  }
  return out.str();
}

}  // namespace can
}  // namespace cockpit
