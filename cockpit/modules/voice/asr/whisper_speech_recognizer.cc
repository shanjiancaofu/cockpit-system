#include "cockpit/modules/voice/asr/whisper_speech_recognizer.h"

#include <whisper.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cockpit {
namespace voice {

WhisperSpeechRecognizer::WhisperSpeechRecognizer(WhisperRecognizerConfig config)
    : config_(std::move(config)) {
  whisper_context_params context_params = whisper_context_default_params();
  context_ = whisper_init_from_file_with_params(config_.model_path.c_str(), context_params);
  if (context_ == nullptr) {
    initialization_error_ = "failed to load whisper.cpp model: " + config_.model_path;
  }
}

WhisperSpeechRecognizer::~WhisperSpeechRecognizer() {
  if (context_ != nullptr) {
    whisper_free(context_);
  }
}

bool WhisperSpeechRecognizer::IsReady() const {
  return context_ != nullptr;
}

const std::string& WhisperSpeechRecognizer::initialization_error() const {
  return initialization_error_;
}

SpeechRecognitionResult WhisperSpeechRecognizer::Recognize(const audio::SpeechSegment& segment) {
  if (context_ == nullptr) {
    return {false, {}, "whisper_cpp", 0.0F, initialization_error_};
  }
  if (segment.samples.empty()) {
    return {false, {}, "whisper_cpp", 0.0F, "speech segment is empty"};
  }

  std::vector<float> pcm;
  pcm.reserve(segment.samples.size());
  for (const std::int16_t sample : segment.samples) {
    pcm.push_back(static_cast<float>(sample) / 32768.0F);
  }

  whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
  params.print_progress = false;
  params.print_realtime = false;
  params.print_timestamps = false;
  params.translate = false;
  params.no_context = true;
  params.single_segment = false;
  params.n_threads = config_.threads;
  params.language = config_.language.c_str();

  if (whisper_full(context_, params, pcm.data(), static_cast<int>(pcm.size())) != 0) {
    return {false, {}, "whisper_cpp", 0.0F, "whisper.cpp inference failed"};
  }

  std::string text;
  const int segment_count = whisper_full_n_segments(context_);
  for (int index = 0; index < segment_count; ++index) {
    const char* segment_text = whisper_full_get_segment_text(context_, index);
    if (segment_text != nullptr) {
      text += segment_text;
    }
  }
  if (text.empty()) {
    return {false, {}, "whisper_cpp", 0.0F, "whisper.cpp returned empty text"};
  }

  return {true, std::move(text), "whisper_cpp", 0.0F, {}};
}

}  // namespace voice
}  // namespace cockpit
