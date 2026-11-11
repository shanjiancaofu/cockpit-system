#include "cockpit/spider/recording/video/h264_gop_ring.h"

#include <algorithm>

namespace cockpit::recording {

H264GopRing::H264GopRing(std::int64_t max_duration_ms, std::uint32_t fps, std::size_t max_bytes)
    : max_duration_pts_(std::max<std::int64_t>(1, max_duration_ms) *
                        std::max<std::uint32_t>(1, fps) / 1000),
      max_bytes_(std::max<std::size_t>(1, max_bytes)) {
}

bool H264GopRing::Append(const EncodedVideoPacket& packet) {
  if (packet.data.empty()) return false;
  if (packet.keyframe && !current_.empty()) {
    gops_.push_back(std::move(current_));
    current_.clear();
    current_bytes_ = 0;
  }
  if (current_.empty() && !packet.keyframe) return false;
  // Never keep an unbounded GOP when IDRs stop arriving. Discard the whole
  // undecodable unit, not individual reference frames. Check before copying.
  if (packet.data.size() > max_bytes_ - current_bytes_ ||
      (!current_.empty() && packet.pts - current_.front().pts > max_duration_pts_)) {
    Clear();
    ++dropped_gops_;
    return false;
  }
  // Evict before allocating the incoming payload, not after exceeding budget.
  Trim(packet.pts, packet.data.size());
  current_.push_back(packet);
  bytes_ += packet.data.size();
  current_bytes_ += packet.data.size();
  ++packets_;
  return true;
}

std::vector<EncodedVideoPacket> H264GopRing::Snapshot() const {
  std::vector<EncodedVideoPacket> result;
  result.reserve(packets_);
  for (const auto& gop : gops_) result.insert(result.end(), gop.begin(), gop.end());
  result.insert(result.end(), current_.begin(), current_.end());
  return result;
}

void H264GopRing::Clear() {
  gops_.clear();
  current_.clear();
  bytes_ = current_bytes_ = packets_ = 0;
}

std::size_t H264GopRing::size() const {
  return packets_;
}

std::int64_t H264GopRing::start_pts() const {
  if (!gops_.empty()) return gops_.front().front().pts;
  return current_.empty() ? -1 : current_.front().pts;
}

void H264GopRing::Trim(std::int64_t newest, std::size_t incoming_bytes) {
  while (!gops_.empty()) {
    const auto& oldest = gops_.front();
    if (bytes_ <= max_bytes_ - incoming_bytes && newest - oldest.front().pts <= max_duration_pts_)
      break;
    for (const auto& packet : oldest) bytes_ -= packet.data.size();
    packets_ -= oldest.size();
    gops_.pop_front();
  }
}

}  // namespace cockpit::recording
