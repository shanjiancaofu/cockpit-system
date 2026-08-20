#include "agent/audio/audio_focus_controller.h"

#include <chrono>
#include <memory>
#include <utility>

namespace cockpit {
namespace voice {
namespace {

class HmiAudioFocusController final : public AudioFocusController {
 public:
  explicit HmiAudioFocusController(std::unique_ptr<HmiCommandProvider> provider)
      : provider_(std::move(provider)) {
  }

  bool AcquireTts() override {
    if (provider_ == nullptr || acquired_) {
      return false;
    }
    ActionExecutionContext context{std::chrono::steady_clock::now() + std::chrono::seconds(1),
                                   std::make_shared<ActionCancellation>()};
    std::string response;
    std::string error;
    acquired_ = provider_->SendCommand(HmiCommand::kPauseMusic, context, &response, &error);
    return acquired_;
  }

  void ReleaseTts() override {
    if (!acquired_ || provider_ == nullptr) {
      return;
    }
    ActionExecutionContext context{std::chrono::steady_clock::now() + std::chrono::seconds(1),
                                   std::make_shared<ActionCancellation>()};
    std::string response;
    std::string error;
    static_cast<void>(provider_->SendCommand(HmiCommand::kResumeMusic, context, &response, &error));
    acquired_ = false;
  }

 private:
  const std::unique_ptr<HmiCommandProvider> provider_;
  bool acquired_ = false;
};

}  // namespace

std::unique_ptr<AudioFocusController> CreateHmiAudioFocusController(
    std::unique_ptr<HmiCommandProvider> provider) {
  return std::make_unique<HmiAudioFocusController>(std::move(provider));
}

}  // namespace voice
}  // namespace cockpit
