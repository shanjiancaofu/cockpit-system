#pragma once

#include <atomic>
#include <memory>
#include <rtc/rtc.hpp>
#include <string>

#include "cockpit/transfer/video/h264_receiver.h"

namespace cockpit::transfer {
std::string H264ProfileLevelId(const std::vector<std::uint8_t>& sps);
rtc::Description::Video SelectH264Video(const rtc::Description& offer,
                                        const std::vector<std::uint8_t>& sps);

// Owned by one peer; Send is called exclusively by that peer's subscription.
class H264Track {
 public:
  H264Track(std::shared_ptr<rtc::PeerConnection> pc, rtc::Description::Video video,
            std::uint64_t generation, unsigned fps, std::function<void()> request_idr = {});
  void Send(VideoFramePtr frame);
  bool waiting_keyframe() const {
    return waiting_->load();
  }

 private:
  std::shared_ptr<rtc::Track> track_;
  std::shared_ptr<rtc::RtpPacketizationConfig> rtp_;
  std::shared_ptr<std::atomic_bool> waiting_ = std::make_shared<std::atomic_bool>(true);
  std::function<void()> request_idr_;
  std::uint64_t generation_;
  unsigned fps_;
  std::int64_t origin_ = -1, last_pts_ = -1;
};
}  // namespace cockpit::transfer
