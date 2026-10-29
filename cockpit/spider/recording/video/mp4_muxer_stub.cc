#include "cockpit/spider/recording/video/mp4_muxer.h"

namespace cockpit::recording {
struct Mp4Muxer::Impl {};
Mp4Muxer::Mp4Muxer() : impl_(std::make_unique<Impl>()) {}
Mp4Muxer::~Mp4Muxer() = default;
bool Mp4Muxer::Open(const std::string&, std::uint32_t, std::string* error) {
  if (error) *error = "GStreamer MP4 muxer dependency is unavailable";
  return false;
}
bool Mp4Muxer::Write(const EncodedVideoPacket&, std::string* error) {
  if (error) *error = "MP4 muxer is unavailable";
  return false;
}
bool Mp4Muxer::Close(std::string*) { return true; }
bool Mp4Muxer::open() const { return false; }
}  // namespace cockpit::recording
