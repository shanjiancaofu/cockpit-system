#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

#include "cockpit/spider/recording/video/h264_gop_ring.h"
#include "cockpit/spider/recording/video/mp4_muxer.h"

namespace cockpit::recording {

struct EventVideoRecorderConfig {
  std::filesystem::path output_directory;
  std::int64_t pre_event_ms = 10'000;
  std::int64_t post_event_ms = 20'000;
  std::uint32_t fps = 30;
};

class EventVideoRecorder final {
 public:
  explicit EventVideoRecorder(EventVideoRecorderConfig config);
  ~EventVideoRecorder();
  EventVideoRecorder(const EventVideoRecorder&) = delete;
  EventVideoRecorder& operator=(const EventVideoRecorder&) = delete;

  bool Append(const EncodedVideoPacket& packet, std::string* error = nullptr);
  bool Trigger(const std::string& event_id, std::int64_t timestamp_pts,
               std::string* output_path = nullptr, std::string* error = nullptr);
  bool Finish(std::string* error = nullptr);
  bool active() const;

 private:
  bool FinishLocked(std::string* error);

  const EventVideoRecorderConfig config_;
  mutable std::mutex mutex_;
  H264GopRing ring_;
  Mp4Muxer muxer_;
  bool active_ = false;
  std::int64_t finish_pts_ = 0;
  std::filesystem::path output_path_;
};

}  // namespace cockpit::recording
