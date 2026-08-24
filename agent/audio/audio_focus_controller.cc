#include "agent/audio/audio_focus_controller.h"

#include <chrono>
#include <memory>
#include <thread>
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
    // HMI play_music is accepted before MediaControlModel's worker publishes PLAYING. Retry the
    // fixed pause command for a short bounded window so TTS does not race the media start.
    for (int attempt = 0; attempt < 20 && !acquired_; ++attempt) {
      ActionExecutionContext context{
          std::chrono::steady_clock::now() + std::chrono::milliseconds(150),
          std::make_shared<ActionCancellation>()};
      std::string response;
      std::string error;
      acquired_ = provider_->SendCommand(HmiCommand::kPauseMusic, context, &response, &error);
      if (!acquired_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
      }
    }
    return acquired_;
  }

  void ReleaseTts() override {
    if (!acquired_ || provider_ == nullptr) {
      return;
    }
    for (int attempt = 0; attempt < 20; ++attempt) {
      ActionExecutionContext context{
          std::chrono::steady_clock::now() + std::chrono::milliseconds(150),
          std::make_shared<ActionCancellation>()};
      std::string response;
      std::string error;
      if (provider_->SendCommand(HmiCommand::kResumeMusic, context, &response, &error)) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
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
