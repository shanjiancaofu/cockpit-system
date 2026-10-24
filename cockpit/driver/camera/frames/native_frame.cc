#include "cockpit/driver/camera/frames/native_frame.h"

#include <stdexcept>
#include <utility>

namespace cockpit::camera {
struct FrameLease::State {
  NativeFrame frame;
  std::function<void()> return_buffer;
  ~State() noexcept {
    return_buffer();
  }
};
FrameLease::FrameLease() = default;
FrameLease::FrameLease(NativeFrame frame, std::function<void()> return_buffer) {
  if (!return_buffer) throw std::invalid_argument("FrameLease requires a return callback");
  state_ = std::unique_ptr<State>(new State{std::move(frame), std::move(return_buffer)});
}
FrameLease::~FrameLease() = default;
FrameLease::FrameLease(FrameLease&&) noexcept = default;
FrameLease& FrameLease::operator=(FrameLease&&) noexcept = default;
FrameLease::operator bool() const noexcept {
  return state_ != nullptr;
}
const NativeFrame& FrameLease::frame() const {
  if (!state_) throw std::logic_error("empty FrameLease");
  return state_->frame;
}
void FrameLease::Reset() noexcept {
  state_.reset();
}
}  // namespace cockpit::camera
