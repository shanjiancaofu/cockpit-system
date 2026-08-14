#include "agent/speech/kws/mock_wake_word_detector.h"

#include <utility>

namespace cockpit {
namespace agent {

MockWakeWordDetector::MockWakeWordDetector(std::string keyword) : keyword_(std::move(keyword)) {
}

WakeWordResult MockWakeWordDetector::Analyze(const audio::AudioFrame&) {
  if (frames_until_detection_ == 0U) {
    return {};
  }
  --frames_until_detection_;
  if (frames_until_detection_ != 0U) {
    return {};
  }
  return {true, keyword_, {}};
}

void MockWakeWordDetector::Reset() {
}

void MockWakeWordDetector::ArmAfterFrames(std::uint64_t frames) {
  frames_until_detection_ = frames;
}

}  // namespace agent
}  // namespace cockpit
