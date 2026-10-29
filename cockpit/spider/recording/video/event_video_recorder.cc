#include "cockpit/spider/recording/video/event_video_recorder.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace cockpit::recording {

namespace {
std::string SafeEventId(const std::string& event_id) {
  std::string result;
  result.reserve(event_id.size());
  for (const unsigned char character : event_id) {
    result.push_back(std::isalnum(character) || character == '-' || character == '_' ?
                         static_cast<char>(character) : '_');
  }
  return result;
}
}  // namespace

EventVideoRecorder::EventVideoRecorder(EventVideoRecorderConfig config)
    : config_(std::move(config)),
      ring_(config_.pre_event_ms, config_.fps) {}

EventVideoRecorder::~EventVideoRecorder() { Finish(); }

bool EventVideoRecorder::Append(const EncodedVideoPacket& packet, std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!ring_.Append(packet)) {
    if (packet.data.empty()) {
      if (error) *error = "encoded video packet is empty";
      return false;
    }
    return true;
  }
  if (!active_) return true;
  if (!muxer_.Write(packet, error)) return false;
  if (packet.pts >= finish_pts_) return FinishLocked(error);
  return true;
}

bool EventVideoRecorder::Trigger(const std::string& event_id, std::int64_t timestamp_pts,
                                 std::string* output_path, std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (event_id.empty() || timestamp_pts < 0 || config_.output_directory.empty() || config_.fps == 0) {
    if (error) *error = "invalid event video trigger";
    return false;
  }
  if (active_ && !FinishLocked(error)) return false;
  output_path_ = config_.output_directory / (SafeEventId(event_id) + ".mp4");
  if (!muxer_.Open(output_path_.string(), config_.fps, error)) return false;
  for (const auto& packet : ring_.Snapshot()) {
    if (!muxer_.Write(packet, error)) {
      muxer_.Close();
      return false;
    }
  }
  const auto post_pts = std::max<std::int64_t>(1, config_.post_event_ms) * config_.fps / 1000;
  finish_pts_ = timestamp_pts + post_pts;
  active_ = true;
  if (output_path) *output_path = output_path_.string();
  return true;
}

bool EventVideoRecorder::Finish(std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  return FinishLocked(error);
}

bool EventVideoRecorder::FinishLocked(std::string* error) {
  if (!active_) return true;
  active_ = false;
  return muxer_.Close(error);
}

bool EventVideoRecorder::active() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_;
}

}  // namespace cockpit::recording
