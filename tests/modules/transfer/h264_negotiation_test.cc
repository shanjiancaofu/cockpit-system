#include <iostream>
#include <stdexcept>

#include "cockpit/transfer/webrtc/h264_track.h"

namespace {
using namespace cockpit::transfer;
using Direction = rtc::Description::Direction;
void Check(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
rtc::Description Offer(const std::string& fmtp, Direction direction = Direction::RecvOnly) {
  rtc::Description offer("v=0\r\no=- 1 1 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n", "offer");
  rtc::Description::Video video("0", direction);
  video.addH264Codec(102, fmtp);
  offer.addMedia(video);
  return offer;
}
bool Accept(const std::string& fmtp, std::vector<std::uint8_t> sps = {0x67, 0x42, 0xc0, 0x28},
            Direction dir = Direction::RecvOnly) {
  try {
    SelectH264Video(Offer(fmtp, dir), sps);
    return true;
  } catch (const std::invalid_argument&) {
    return false;
  }
}
}  // namespace
int main() {
  try {
    const std::string base = "profile-level-id=42e01f;packetization-mode=1";
    Check(!Accept(base), "default Level 3.1 falsely accepted 1080P Level 4.0");
    Check(!Accept(base + ";level-asymmetry-allowed=1"),
          "asymmetry alone falsely accepted higher receive Level");
    Check(Accept(base + ";max-recv-level=e028"), "explicit recvonly Level 4.0 not honored");
    Check(Accept(base + ";max-recv-level=e028;level-asymmetry-allowed=0"),
          "recvonly incorrectly requires asymmetry");
    Check(!Accept(base + ";max-recv-level=e028", {0x67, 0x42, 0xc0, 0x28}, Direction::SendRecv),
          "symmetric bidirectional Level upgrade accepted");
    Check(Accept(base + ";max-recv-level=e028;level-asymmetry-allowed=1", {0x67, 0x42, 0xc0, 0x28},
                 Direction::SendRecv),
          "legal asymmetric receive limit rejected");
    Check(!Accept(base + ";max-fs=8192;max-mbps=245760"),
          "partial receive limits invented DPB/CPB/bitrate support");
    Check(Accept(base + ";max-recv-level=e028;max-fs=8192;max-mbps=245760"),
          "valid combined receive limits rejected");
    for (const auto& parameter :
         {"max-recv-level=e01f", "max-recv-level=e00b", "max-recv-level=42e028",
          "max-recv-level=xxxx", "max-recv-level=e027", "max-fs=1", "max-fs=-8192",
          "max-fs=8192junk", "max-mbps=0", "max-mbps=99999999999999999999",
          "max-recv-level=e028;max-fs=3600", "max-recv-level=e028;max-mbps=108000",
          "max-recv-level=e028;max-recv-level=e029"})
      Check(!Accept(base + ";" + parameter), parameter);
    Check(!Accept("profile-level-id=640028;packetization-mode=1"), "profile mismatch accepted");
    Check(!Accept("profile-level-id=42e028;packetization-mode=0"),
          "packetization-mode mismatch accepted");
    Check(!Accept("packetization-mode=1"),
          "absent profile-level-id defaulted to 720P instead of RFC Level 1");
    Check(!Accept(base + ";max-recv-level=e028;level-asymmetry-allowed=2", {0x67, 0x42, 0xc0, 0x28},
                  Direction::SendRecv),
          "invalid asymmetry accepted");
    // Correct ordering of Level 1b: greater than 1, lower than 1.1.
    Check(Accept("profile-level-id=42e00b;packetization-mode=1", {0x67, 0x42, 0xf0, 0x0b}),
          "Level 1b rejected by Level 1.1 receiver");
    Check(!Accept("profile-level-id=42f00b;packetization-mode=1", {0x67, 0x42, 0xe0, 0x0b}),
          "Level 1b receiver falsely accepted Level 1.1");
    Check(Accept("profile-level-id=42e00a;packetization-mode=1;max-recv-level=f00b",
                 {0x67, 0x42, 0xf0, 0x0b}),
          "explicit max-recv Level 1b not honored");
    auto alternatives = Offer(base + ";max-recv-level=bad");
    auto* alternative_media = std::get<rtc::Description::Media*>(alternatives.media(0));
    rtc::Description::Video fallback("0");
    fallback.addH264Codec(103, "profile-level-id=42e028;packetization-mode=1");
    alternative_media->addRtpMap(*fallback.rtpMap(103));
    Check(SelectH264Video(alternatives, {0x67, 0x42, 0xc0, 0x28}).hasPayloadType(103),
          "malformed first payload hid a compatible alternative");
    const auto answer =
        SelectH264Video(Offer(base + ";max-recv-level=e028"), {0x67, 0x42, 0xc0, 0x28});
    const auto* codec = answer.rtpMap(102);
    Check(codec && codec->fmtps.front().find("42c028") != std::string::npos,
          "answer falsified SPS profile/level");
    Check(codec->fmtps.front().find("max-recv-level") == std::string::npos,
          "answer copied receiver-only capabilities");
    std::cout << "RFC6184 receive limits, directionality, malformed fmtp and Level1b passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
