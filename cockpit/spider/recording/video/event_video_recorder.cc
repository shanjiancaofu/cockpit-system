#include "cockpit/spider/recording/video/event_video_recorder.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <utility>

namespace cockpit::recording {

namespace {
std::string SafeEventId(const std::string& event_id) {
  std::string result;
  result.reserve(event_id.size());
  for (const unsigned char character : event_id) {
    result.push_back(std::isalnum(character) || character == '-' || character == '_'
                         ? static_cast<char>(character)
                         : '_');
  }
  return result;
}
}  // namespace

EventVideoRecorder::EventVideoRecorder(EventVideoRecorderConfig config)
    : config_(std::move(config)),
      ring_(config_.pre_event_ms, config_.fps, config_.max_history_bytes) {
}

EventVideoRecorder::~EventVideoRecorder() {
  Finish();
}

bool EventVideoRecorder::Append(const EncodedVideoPacket& packet, std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (packet.data.empty()) {
    if (active_) return FailLocked("encoded video packet is empty", error);
    if (error) *error = "encoded video packet is empty";
    return false;
  }
  ring_.Append(packet);
  // History eviction must not drop live packets from an active recording.
  if (!active_) return true;
  std::string failure;
  if (!muxer_.Write(packet, &failure)) return FailLocked(failure, error);
  if (packet.pts >= finish_pts_) return FinishLocked(error);
  return true;
}

bool EventVideoRecorder::Trigger(const std::string& event_id, std::int64_t timestamp_pts,
                                 std::string* output_path, std::string* error,
                                 const std::filesystem::path& output_directory) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto directory = output_directory.empty() ? config_.output_directory : output_directory;
  if (event_id.empty() || timestamp_pts < 0 || directory.empty() || config_.fps == 0 ||
      ring_.size() == 0) {
    if (error) *error = "invalid event video trigger";
    return false;
  }
  if (active_ && !FinishLocked(error)) return false;
  failure_.clear();
  output_path_ = directory / (SafeEventId(event_id) + ".mp4");
  temporary_path_ = output_path_.string() + ".incomplete";
  std::error_code filesystem_error;
  for (const auto& path : {output_path_, temporary_path_}) {
    const bool exists = std::filesystem::exists(path, filesystem_error);
    if (filesystem_error || exists)
      return FailLocked(
          filesystem_error ? filesystem_error.message() : "event video already exists", error);
  }
  std::filesystem::create_directories(directory, filesystem_error);
  if (filesystem_error) return FailLocked(filesystem_error.message(), error);
  // Reserve staging exclusively so concurrent recorders cannot truncate the
  // same incomplete artifact after the existence check above.
  const int staging =
      ::open(temporary_path_.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (staging < 0) return FailLocked(std::strerror(errno), error);
  if (::close(staging) != 0) return FailLocked(std::strerror(errno), error);
  std::string failure;
  if (!muxer_.Open(temporary_path_.string(), config_.fps, &failure))
    return FailLocked(failure, error);
  for (const auto& packet : ring_.Snapshot()) {
    if (!muxer_.Write(packet, &failure)) return FailLocked(failure, error);
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

bool EventVideoRecorder::FailLocked(const std::string& failure, std::string* error) {
  if (failure_.empty()) failure_ = failure.empty() ? "event video failed" : failure;
  active_ = false;
  std::string close_error;
  if (!muxer_.Close(&close_error) && !close_error.empty() && close_error != failure_)
    failure_ += "; close: " + close_error;
  if (error) *error = failure_;
  // Retain the explicitly named .incomplete for diagnosis; never publish it.
  return false;
}

bool EventVideoRecorder::FinishLocked(std::string* error) {
  if (!failure_.empty()) {
    if (error) *error = failure_;
    return false;
  }
  if (!active_) return true;
  std::string failure;
  if (!muxer_.Close(&failure)) return FailLocked(failure, error);
  active_ = false;
  // EOS confirms mux completion. Also check top-level MP4 boxes before publishing
  // (this is structural validation, not proof of successful video decoding).
  std::ifstream input(temporary_path_, std::ios::binary | std::ios::ate);
  const auto end = input.tellg();
  bool ftyp = false, moov = false, mdat = false;
  std::uint64_t offset = 0;
  while (input && end > 0 && offset < static_cast<std::uint64_t>(end)) {
    input.seekg(offset);
    unsigned char header[16]{};
    input.read(reinterpret_cast<char*>(header), 8);
    if (!input) break;
    auto read_size = [&](int first, int last) {
      std::uint64_t size = 0;
      for (int i = first; i < last; ++i) size = (size << 8) | header[i];
      return size;
    };
    std::uint64_t size = read_size(0, 4), header_size = 8;
    if (size == 1) {
      input.read(reinterpret_cast<char*>(header + 8), 8);
      if (!input) break;
      size = read_size(8, 16);
      header_size = 16;
    } else if (size == 0) {
      size = static_cast<std::uint64_t>(end) - offset;
    }
    if (size < header_size || size > static_cast<std::uint64_t>(end) - offset) break;
    const std::string type(reinterpret_cast<char*>(header + 4), 4);
    ftyp |= type == "ftyp";
    moov |= type == "moov";
    mdat |= type == "mdat" && size > header_size;
    offset += size;
  }
  if (!input || end <= 0 || offset != static_cast<std::uint64_t>(end) || !ftyp || !moov || !mdat)
    return FailLocked("completed MP4 failed structural validation", error);
  input.close();
  // Atomic no-replace publication on the same filesystem. Do not overwrite an
  // earlier event when a filename is reused. Session finalization owns fsync.
  if (::link(temporary_path_.c_str(), output_path_.c_str()) != 0)
    return FailLocked(std::string("publish MP4 failed: ") + std::strerror(errno), error);
  std::error_code filesystem_error;
  std::filesystem::remove(temporary_path_, filesystem_error);
  if (filesystem_error) {
    std::error_code ignored;
    std::filesystem::remove(output_path_, ignored);
    return FailLocked(filesystem_error.message(), error);
  }
  return true;
}

std::int64_t EventVideoRecorder::HistoryStartPts() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return ring_.start_pts();
}

std::uint64_t EventVideoRecorder::HistoryDroppedGops() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return ring_.dropped_gops();
}

void EventVideoRecorder::ClearHistory() {
  std::lock_guard<std::mutex> lock(mutex_);
  ring_.Clear();
}

bool EventVideoRecorder::active() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_;
}

}  // namespace cockpit::recording
