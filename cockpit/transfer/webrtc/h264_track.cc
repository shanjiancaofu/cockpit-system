#include "cockpit/transfer/webrtc/h264_track.h"

#include <cctype>
#include <charconv>
#include <iomanip>
#include <limits>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>

namespace cockpit::transfer {
namespace {
std::map<std::string, std::string> Parameters(const std::vector<std::string>& fmtps,
                                              bool bidirectional) {
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
      for (auto& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      const auto inserted = result.emplace(key, value);
      if (!inserted.second && inserted.first->second != value &&
          (key == "profile-level-id" || key == "packetization-mode" || key == "max-recv-level" ||
           (bidirectional && key == "level-asymmetry-allowed") || key == "max-fs" ||
           key == "max-mbps"))
        throw std::invalid_argument("conflicting duplicate H264 fmtp parameter: " + key);
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
// RFC 6184 section 8.1 / H.264 Annex A. Level 1b orders between 1 and 1.1;
// comparing only the raw level_idc byte is not sufficient.
struct LevelLimits {
  unsigned idc, rank, fs, mbps;
};
LevelLimits Level(unsigned iop, unsigned idc) {
  if ((idc == 11 && (iop & 0x10)) || (idc == 9 && !(iop & 0x10))) return {idc, 105, 99, 1485};
  static constexpr LevelLimits levels[] = {
      {10, 100, 99, 1485},        {11, 110, 396, 3000},       {12, 120, 396, 6000},
      {13, 130, 396, 11880},      {20, 200, 396, 11880},      {21, 210, 792, 19800},
      {22, 220, 1620, 20250},     {30, 300, 1620, 40500},     {31, 310, 3600, 108000},
      {32, 320, 5120, 216000},    {40, 400, 8192, 245760},    {41, 410, 8192, 245760},
      {42, 420, 8704, 522240},    {50, 500, 22080, 589824},   {51, 510, 36864, 983040},
      {52, 520, 36864, 2073600},  {60, 600, 139264, 4177920}, {61, 610, 139264, 8355840},
      {62, 620, 139264, 16711680}};
  for (const auto& level : levels)
    if (level.idc == idc) return level;
  throw std::invalid_argument("unknown H264 level_idc");
}
unsigned Hex(const std::string& text, std::size_t digits) {
  if (text.size() != digits)
    throw std::invalid_argument("invalid H264 hexadecimal parameter length");
  unsigned value = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, 16);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    throw std::invalid_argument("invalid H264 hexadecimal parameter");
  return value;
}
unsigned Decimal(const std::string& text) {
  unsigned value = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !value ||
      value > static_cast<unsigned>(std::numeric_limits<int>::max()))
    throw std::invalid_argument("invalid H264 receive limit");
  return value;
}
struct ReceiveCapability {
  LevelLimits base, highest;
  bool asymmetric;
};
ReceiveCapability ReceiveLimits(const std::map<std::string, std::string>& params,
                                bool bidirectional) {
  // RFC 6184 default without profile-level-id: unconstrained Baseline Level 1.
  const auto profile = params.find("profile-level-id");
  const auto id = Hex(profile == params.end() ? "42000a" : profile->second, 6);
  ReceiveCapability capability{Level((id >> 8) & 255, id & 255), {}, false};
  capability.highest = capability.base;
  if (const auto asym = params.find("level-asymmetry-allowed");
      bidirectional && asym != params.end()) {
    if (asym->second != "0" && asym->second != "1")
      throw std::invalid_argument("invalid level-asymmetry-allowed");
    capability.asymmetric = asym->second == "1";
  }
  if (const auto receive = params.find("max-recv-level"); receive != params.end()) {
    const auto value =
        Hex(receive->second, 4);  // profile-iop + level_idc, NOT decimal / three bytes
    capability.highest = Level(value >> 8, value & 255);
    if (capability.highest.rank <= capability.base.rank)
      throw std::invalid_argument("max-recv-level must exceed the default level");
  }
  for (const auto& limit : {std::pair<const char*, unsigned>{"max-fs", capability.highest.fs},
                            {"max-mbps", capability.highest.mbps}}) {
    if (const auto found = params.find(limit.first); found != params.end()) {
      if (Decimal(found->second) < limit.second)
        throw std::invalid_argument("receive extension is smaller than its declared level");
    }
  }
  return capability;
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
  if (sps.size() < 4 || (sps[0] & 0x9f) != 7 || offer.mediaCount() != 1)
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
    try {
      const bool bidirectional = media->direction() == rtc::Description::Direction::SendRecv;
      const auto params = Parameters(map->fmtps, bidirectional);
      const auto mode = params.find("packetization-mode");
      if (mode == params.end() || mode->second != "1") continue;
      const auto found = params.find("profile-level-id");
      const auto value = Hex(found == params.end() ? "42000a" : found->second, 6);
      const auto actual = Profile(sps[1], sps[2]);
      if (!actual || actual != Profile(value >> 16, (value >> 8) & 255)) continue;
      const auto capability = ReceiveLimits(params, bidirectional);
      const auto stream = Level(sps[2], sps[3]);
      // Partial max-fs/max-mbps extensions do not certify the stream's other
      // Level constraints (DPB/CPB/bitrate). Do not invent a full higher Level.
      if (stream.rank > capability.highest.rank) continue;
      // RFC 6184 Table 6: level-asymmetry-allowed is ignored for recvonly.
      // For a bidirectional offer, absent/false asymmetry forbids Level upgrade.
      if (bidirectional && !capability.asymmetric && stream.rank > capability.base.rank) continue;
      rtc::Description::Video video(media->mid(), rtc::Description::Direction::SendOnly);
      const std::string asymmetry =
          bidirectional
              ? "level-asymmetry-allowed=" + std::string(capability.asymmetric ? "1" : "0") + ";"
              : "";
      video.addH264Codec(
          pt, asymmetry + "packetization-mode=1;profile-level-id=" + H264ProfileLevelId(sps));
      return video;
    } catch (const std::invalid_argument&) {
      // Malformed capabilities invalidate this payload type, not other valid
      // H264 alternatives in the same m-line.
    }
  }
  throw std::invalid_argument(
      "H264 receive capability incompatible: stream=" + H264ProfileLevelId(sps) +
      "; require matching profile, packetization-mode=1 and sufficient declared receive Level "
      "(max-recv-level is honored; level-asymmetry or max-fs/max-mbps alone do not certify "
      "a higher full Level). Use a compatible receiver or restart Camera at 720P; "
      "the existing stream is not transcoded.");
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
