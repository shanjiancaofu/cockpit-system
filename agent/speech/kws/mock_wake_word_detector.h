#pragma once

#include <cstdint>
#include <string>

#include "agent/speech/kws/wake_word_detector.h"

namespace cockpit {
namespace agent {

class MockWakeWordDetector final : public WakeWordDetector {
 public:
  explicit MockWakeWordDetector(std::string keyword = "");

  WakeWordResult Analyze(const audio::AudioFrame& frame) override;
  void Reset() override;

  void ArmAfterFrames(std::uint64_t frames);

 private:
  std::string keyword_;
  std::uint64_t frames_until_detection_ = 0;
};

}  // namespace agent
}  // namespace cockpit
