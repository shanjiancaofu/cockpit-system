#include "cockpit/modules/audio/vad/speech_segmenter.h"

#include <cstdint>
#include <iostream>

namespace {

cockpit::audio::AudioFrame MakeFrame(
    std::uint64_t sequence,
    cockpit::audio::AudioFrameFlag flags = cockpit::audio::AudioFrameFlag::kNone) {
  cockpit::audio::AudioFrame::Samples samples{};
  samples.fill(static_cast<std::int16_t>(sequence));
  return cockpit::audio::AudioFrame(sequence, static_cast<std::int64_t>(sequence * 20000000ULL),
                                    flags, samples);
}

cockpit::audio::VoiceActivityResult Activity(cockpit::audio::VoiceActivityState state,
                                             bool changed = false) {
  return {state, state == cockpit::audio::VoiceActivityState::kSpeech ? -10.0 : -120.0, changed};
}

bool TestPreRollAndEndpoint() {
  cockpit::audio::SpeechSegmenter segmenter({2, 10});
  segmenter.Process(MakeFrame(0), Activity(cockpit::audio::VoiceActivityState::kSilence));
  segmenter.Process(MakeFrame(1), Activity(cockpit::audio::VoiceActivityState::kSilence));
  segmenter.Process(MakeFrame(2), Activity(cockpit::audio::VoiceActivityState::kSilence));
  segmenter.Process(MakeFrame(3), Activity(cockpit::audio::VoiceActivityState::kSpeech, true));
  segmenter.Process(MakeFrame(4), Activity(cockpit::audio::VoiceActivityState::kSpeech));
  auto segment =
      segmenter.Process(MakeFrame(5), Activity(cockpit::audio::VoiceActivityState::kSilence, true));
  return segment.has_value() && segment->start_sequence == 1 && segment->end_sequence == 4 &&
         segment->FrameCount() == 4 && segment->DurationMs() == 80 && !segment->truncated &&
         !segment->discontinuous && segment->samples.front() == 1;
}

bool TestDiscontinuity() {
  cockpit::audio::SpeechSegmenter segmenter({1, 10});
  segmenter.Process(MakeFrame(0), Activity(cockpit::audio::VoiceActivityState::kSilence));
  segmenter.Process(MakeFrame(1), Activity(cockpit::audio::VoiceActivityState::kSpeech, true));
  segmenter.Process(MakeFrame(2), Activity(cockpit::audio::VoiceActivityState::kSpeech));
  auto segment = segmenter.Process(MakeFrame(3, cockpit::audio::AudioFrameFlag::kDiscontinuity),
                                   Activity(cockpit::audio::VoiceActivityState::kSilence));
  return segment.has_value() && segment->start_sequence == 0 && segment->end_sequence == 2 &&
         segment->FrameCount() == 3 && segment->discontinuous && !segment->truncated;
}

bool TestIdleDiscontinuityPreRoll() {
  cockpit::audio::SpeechSegmenter segmenter({2, 10});
  segmenter.Process(MakeFrame(0, cockpit::audio::AudioFrameFlag::kDiscontinuity),
                    Activity(cockpit::audio::VoiceActivityState::kSilence));
  segmenter.Process(MakeFrame(1), Activity(cockpit::audio::VoiceActivityState::kSpeech, true));
  auto segment =
      segmenter.Process(MakeFrame(2), Activity(cockpit::audio::VoiceActivityState::kSilence, true));
  return segment.has_value() && segment->FrameCount() == 2 && segment->start_sequence == 0 &&
         segment->end_sequence == 1;
}

bool TestTruncationAndFlush() {
  cockpit::audio::SpeechSegmenter segmenter({0, 3});
  segmenter.Process(MakeFrame(0), Activity(cockpit::audio::VoiceActivityState::kSpeech, true));
  segmenter.Process(MakeFrame(1), Activity(cockpit::audio::VoiceActivityState::kSpeech));
  auto truncated =
      segmenter.Process(MakeFrame(2), Activity(cockpit::audio::VoiceActivityState::kSpeech));
  if (!truncated.has_value() || !truncated->truncated || truncated->FrameCount() != 3) {
    return false;
  }
  segmenter.Process(MakeFrame(3), Activity(cockpit::audio::VoiceActivityState::kSpeech));
  auto flushed = segmenter.Flush();
  return flushed.has_value() && !flushed->truncated && !flushed->discontinuous &&
         flushed->FrameCount() == 1;
}

}  // namespace

int main() {
  if (!TestPreRollAndEndpoint() || !TestDiscontinuity() || !TestIdleDiscontinuityPreRoll() ||
      !TestTruncationAndFlush()) {
    std::cerr << "speech segmenter tests failed\n";
    return 1;
  }
  std::cout << "speech segmenter tests passed\n";
  return 0;
}
