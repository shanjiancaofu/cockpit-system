#include "cockpit/transfer/webrtc/h264_track.h"

#include <iomanip>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>

namespace cockpit::transfer {
namespace {
std::map<std::string, std::string> Parameters(const std::vector<std::string>& fmtps) {
  std::map<std::string, std::string> result;
  for (const auto& fmtp : fmtps) {
    std::istringstream input(fmtp);
    std::string parameter;
    while (std::getline(input, parameter, ';')) {
      const auto eq = parameter.find('=');
      if (eq == std::string::npos) continue;
      auto key = parameter.substr(0, eq), value = parameter.substr(eq + 1);
      const auto first = key.find_first_not_of(" \t");
      if (first == std::string::npos) continue;
      key = key.substr(first, key.find_last_not_of(" \t") - first + 1);
      const auto vfirst = value.find_first_not_of(" \t");
      value = vfirst == std::string::npos
                  ? ""
                  : value.substr(vfirst, value.find_last_not_of(" \t") - vfirst + 1);
      result[key] = value;
    }
  }
  return result;
}
int Profile(unsigned idc, unsigned constraints) {
  if ((idc == 0x42 && (constraints & 0x4f) == 0x40) ||
      (idc == 0x4d && (constraints & 0x8f) == 0x80) ||
      (idc == 0x58 && (constraints & 0xcf) == 0xc0))
    return 1;
  if ((idc == 0x42 && (constraints & 0x4f) == 0) || (idc == 0x58 && (constraints & 0xcf) == 0x80))
    return 2;
  if (idc == 0x4d && (constraints & 0xaf) == 0) return 3;
  if (idc == 0x64 && (constraints & 0x0f) == 0) return 4;
  return 0;
}
class KeyframeFeedback final : public rtc::MediaHandler {
 public:
  explicit KeyframeFeedback(std::shared_ptr<std::atomic_bool> waiting,
                            std::function<void()> request_idr)
      : waiting_(std::move(waiting)), request_idr_(std::move(request_idr)) {
  }
  void incoming(rtc::message_vector& messages, const rtc::message_callback&) override {
    for (const auto& message : messages) {
      const auto* data = reinterpret_cast<const std::uint8_t*>(message->data());
      for (std::size_t offset = 0; offset + 4 <= message->size();) {
        const auto* p = data + offset;
        const auto length = (std::size_t(p[2]) * 256 + p[3] + 1) * 4;
        if (length < 4 || offset + length > message->size()) break;
        if ((p[0] >> 6) == 2 && p[1] == 206 && ((p[0] & 31) == 1 || (p[0] & 31) == 4)) {
          waiting_->store(true);
          if (request_idr_) request_idr_();
        }
        offset += length;
      }
    }
  }

 private:
  std::shared_ptr<std::atomic_bool> waiting_;
  std::function<void()> request_idr_;
};
}  // namespace

std::string H264ProfileLevelId(const std::vector<std::uint8_t>& sps) {
  if (sps.size() < 4 || (sps[0] & 31) != 7) return {};
  std::ostringstream text;
  // High-profile constraint_set3 does not change the negotiated High profile.
  text << std::hex << std::setfill('0') << std::setw(2) << unsigned(sps[1]) << std::setw(2)
       << unsigned(sps[1] == 0x64 ? sps[2] & 0xef : sps[2]) << std::setw(2) << unsigned(sps[3]);
  return text.str();
}
rtc::Description::Video SelectH264Video(const rtc::Description& offer,
                                        const std::vector<std::uint8_t>& sps) {
  if (sps.size() < 4 || offer.mediaCount() != 1)
    throw std::invalid_argument("offer must contain one receiving video m-line");
  const auto entry = offer.media(0);
  const auto* media_ptr = std::get_if<const rtc::Description::Media*>(&entry);
  if (!media_ptr || (*media_ptr)->type() != "video" || (*media_ptr)->isRemoved() ||
      ((*media_ptr)->direction() != rtc::Description::Direction::RecvOnly &&
       (*media_ptr)->direction() != rtc::Description::Direction::SendRecv))
    throw std::invalid_argument("offer must receive video");
  const auto* media = *media_ptr;
  for (const auto pt : media->payloadTypes()) {
    const auto* map = media->rtpMap(pt);
    if (!map || map->format != "H264" || map->clockRate != 90000 || pt < 0 || pt > 127) continue;
    const auto params = Parameters(map->fmtps);
    const auto mode = params.find("packetization-mode");
    if (mode == params.end() || mode->second != "1") continue;
    const auto found = params.find("profile-level-id");
    const auto profile = found == params.end() ? "42e01f" : found->second;
    if (profile.size() != 6 ||
        profile.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
      continue;
    const auto value = static_cast<unsigned>(std::stoul(profile, nullptr, 16));
    const auto actual = Profile(sps[1], sps[2]);
    if (!actual || actual != Profile(value >> 16, (value >> 8) & 255) || sps[3] > (value & 255))
      continue;
    rtc::Description::Video video(media->mid(), rtc::Description::Direction::SendOnly);
    video.addH264Codec(pt, "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=" +
                               H264ProfileLevelId(sps));
    return video;
  }
  throw std::invalid_argument(
      "no compatible H264 packetization-mode=1/profile/level in offer; stream=" +
      H264ProfileLevelId(sps));
}
H264Track::H264Track(std::shared_ptr<rtc::PeerConnection> pc, rtc::Description::Video video,
                     std::uint64_t generation, unsigned fps, std::function<void()> request_idr)
    : request_idr_(std::move(request_idr)), generation_(generation), fps_(fps) {
  const auto ssrc = std::random_device{}();
  const auto pt = static_cast<std::uint8_t>(video.payloadTypes().front());
  video.addSSRC(ssrc, "cockpit", "camera", "video");
  track_ = pc->addTrack(video);
  rtp_ = std::make_shared<rtc::RtpPacketizationConfig>(ssrc, "cockpit", pt, 90000);
  auto packetizer =
      std::make_shared<rtc::H264RtpPacketizer>(rtc::NalUnit::Separator::StartSequence, rtp_, 1000);
  packetizer->addToChain(std::make_shared<rtc::RtcpSrReporter>(rtp_));
  packetizer->addToChain(std::make_shared<rtc::RtcpNackResponder>(512));
  packetizer->addToChain(std::make_shared<KeyframeFeedback>(waiting_, request_idr_));
  track_->setMediaHandler(packetizer);
  track_->onOpen([waiting = waiting_, request = request_idr_] {
    waiting->store(true);
    if (request) request();
  });
}
void H264Track::Send(VideoFramePtr frame) {
  if (frame->generation != generation_) return;
  if (frame->discontinuity) waiting_->store(true);
  if (!track_->isOpen() || track_->bufferedAmount() > 1024 * 1024) {
    waiting_->store(true);
    if (track_->isOpen() && request_idr_) request_idr_();
    return;
  }
  if (waiting_->load()) {
    if (!frame->packet.keyframe || frame->sps.size() < 4 || frame->pps.empty()) return;
    waiting_->store(false);
  }
  if (origin_ < 0) origin_ = frame->packet.pts;
  if (frame->packet.pts < origin_ || frame->packet.pts <= last_pts_) {
    waiting_->store(true);
    return;
  }
  last_pts_ = frame->packet.pts;
  // Integer conversion preserves encoder tick spacing and wraps naturally at 32 bits.
  const auto ticks = static_cast<std::uint64_t>(frame->packet.pts - origin_);
  const auto timestamp =
      rtp_->startTimestamp +
      static_cast<std::uint32_t>((ticks / fps_) * 90000 + (ticks % fps_) * 90000 / fps_);
  try {
    track_->sendFrame(reinterpret_cast<const std::byte*>(frame->packet.data.data()),
                      frame->packet.data.size(), rtc::FrameInfo(timestamp));
  } catch (const std::exception&) {
    waiting_->store(true);
    if (request_idr_) request_idr_();
  }
}
}  // namespace cockpit::transfer
