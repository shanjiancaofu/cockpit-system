#include "modules/audio/vad/speech_segmenter.h"

#include <utility>

namespace cockpit {
namespace audio {

SpeechSegmenter::SpeechSegmenter(SpeechSegmenterConfig config) : config_(config) {
}

std::optional<SpeechSegment> SpeechSegmenter::Process(const AudioFrame& frame,
                                                      const VoiceActivityResult& activity) {
  if (frame.HasFlag(AudioFrameFlag::kDiscontinuity)) {
    auto interrupted = FinishSegment(false, true);
    pre_roll_.clear();
    if (interrupted.has_value()) {
      PushPreRoll(frame);
      return interrupted;
    }
    if (activity.state == VoiceActivityState::kSilence) {
      PushPreRoll(frame);
      return std::nullopt;
    }
  }

  if (!active_segment_.has_value()) {
    if (activity.state == VoiceActivityState::kSilence) {
      PushPreRoll(frame);
      return std::nullopt;
    }
    StartSegment(frame);
  } else if (activity.state == VoiceActivityState::kSpeech) {
    AppendFrame(frame);
  } else {
    auto completed = FinishSegment(false, false);
    PushPreRoll(frame);
    return completed;
  }

  if (active_segment_->FrameCount() >= config_.max_segment_frames) {
    return FinishSegment(true, false);
  }
  return std::nullopt;
}

std::optional<SpeechSegment> SpeechSegmenter::Flush() {
  return FinishSegment(false, false);
}

void SpeechSegmenter::Reset() {
  pre_roll_.clear();
  active_segment_.reset();
}

void SpeechSegmenter::PushPreRoll(const AudioFrame& frame) {
  if (config_.pre_roll_frames == 0) {
    return;
  }
  pre_roll_.push_back(frame);
  while (pre_roll_.size() > config_.pre_roll_frames) {
    pre_roll_.pop_front();
  }
}

void SpeechSegmenter::StartSegment(const AudioFrame& frame) {
  active_segment_.emplace();
  const std::size_t reserve_frames = config_.pre_roll_frames + config_.max_segment_frames;
  active_segment_->samples.reserve(reserve_frames * AudioFrame::kSampleCount);
  for (const auto& pre_roll_frame : pre_roll_) {
    AppendFrame(pre_roll_frame);
  }
  pre_roll_.clear();
  AppendFrame(frame);
}

void SpeechSegmenter::AppendFrame(const AudioFrame& frame) {
  if (active_segment_->samples.empty()) {
    active_segment_->start_sequence = frame.sequence();
    active_segment_->start_time_ns = frame.capture_time_ns();
  }
  active_segment_->end_sequence = frame.sequence();
  active_segment_->end_time_ns = frame.capture_time_ns();
  active_segment_->samples.insert(active_segment_->samples.end(), frame.samples().begin(),
                                  frame.samples().end());
}

std::optional<SpeechSegment> SpeechSegmenter::FinishSegment(bool truncated, bool discontinuous) {
  if (!active_segment_.has_value()) {
    return std::nullopt;
  }
  active_segment_->truncated = truncated;
  active_segment_->discontinuous = discontinuous;
  std::optional<SpeechSegment> completed(std::move(*active_segment_));
  active_segment_.reset();
  return completed;
}

}  // namespace audio
}  // namespace cockpit
