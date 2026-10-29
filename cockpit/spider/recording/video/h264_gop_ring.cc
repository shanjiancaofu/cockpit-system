#include "cockpit/spider/recording/video/h264_gop_ring.h"

#include <algorithm>

namespace cockpit::recording {

H264GopRing::H264GopRing(std::int64_t max_duration_ms, std::uint32_t fps)
    : max_duration_pts_(std::max<std::int64_t>(1, max_duration_ms) * std::max<std::uint32_t>(1, fps) /
                        1000) {}

bool H264GopRing::Append(const EncodedVideoPacket& packet) {
  if (packet.data.empty()) return false;
  if (packet.keyframe && !current_.empty()) {
    gops_.push_back(std::move(current_));
    current_.clear();
  }
  if (current_.empty() && !packet.keyframe) return false;
  current_.push_back(packet);
  Trim();
  return true;
}

std::vector<EncodedVideoPacket> H264GopRing::Snapshot() const {
  std::vector<EncodedVideoPacket> result;
  for (const auto& gop : gops_) result.insert(result.end(), gop.begin(), gop.end());
  result.insert(result.end(), current_.begin(), current_.end());
  return result;
}

void H264GopRing::Clear() {
  gops_.clear();
  current_.clear();
}

std::size_t H264GopRing::size() const {
  std::size_t count = current_.size();
  for (const auto& gop : gops_) count += gop.size();
  return count;
}

void H264GopRing::Trim() {
  const auto snapshot = Snapshot();
  if (snapshot.empty()) return;
  const auto newest = snapshot.back().pts;
  while (!gops_.empty()) {
    const auto& oldest = gops_.front();
    if (oldest.empty() || newest - oldest.front().pts <= max_duration_pts_) break;
    gops_.pop_front();
  }
}

}  // namespace cockpit::recording
