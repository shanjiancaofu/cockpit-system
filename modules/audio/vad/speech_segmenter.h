#pragma once

#include <cstddef>
#include <deque>
#include <optional>

#include "modules/audio/frames/audio_frame.h"
#include "modules/audio/vad/speech_segment.h"
#include "modules/audio/vad/voice_activity_detector.h"

namespace cockpit {
namespace audio {

struct SpeechSegmenterConfig {
  std::size_t pre_roll_frames = 5;
  std::size_t max_segment_frames = 750;
};

class SpeechSegmenter {
 public:
  explicit SpeechSegmenter(SpeechSegmenterConfig config);

  std::optional<SpeechSegment> Process(const AudioFrame& frame,
                                       const VoiceActivityResult& activity);
  std::optional<SpeechSegment> Flush();
  void Reset();

 private:
  void PushPreRoll(const AudioFrame& frame);
  void StartSegment(const AudioFrame& frame);
  void AppendFrame(const AudioFrame& frame);
  std::optional<SpeechSegment> FinishSegment(bool truncated, bool discontinuous);

  const SpeechSegmenterConfig config_;
  std::deque<AudioFrame> pre_roll_;
  std::optional<SpeechSegment> active_segment_;
};

}  // namespace audio
}  // namespace cockpit
