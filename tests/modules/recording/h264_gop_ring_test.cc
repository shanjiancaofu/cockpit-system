#include "cockpit/spider/recording/video/h264_gop_ring.h"

#include <cstdint>
#include <iostream>
#include <vector>

using cockpit::recording::EncodedVideoPacket;
using cockpit::recording::H264GopRing;

int main() {
  H264GopRing ring(2000, 10);
  auto packet = [](std::int64_t pts, bool keyframe) {
    EncodedVideoPacket value;
    value.data = {0, 0, 0, 1, static_cast<std::uint8_t>(keyframe ? 0x65 : 0x41)};
    value.pts = pts;
    value.dts = pts;
    value.keyframe = keyframe;
    return value;
  };
  if (!ring.Append(packet(0, true)) || !ring.Append(packet(1, false)) ||
      !ring.Append(packet(10, true)) || !ring.Append(packet(11, false))) {
    std::cerr << "failed to append GOP packets\n";
    return 1;
  }
  const auto snapshot = ring.Snapshot();
  if (snapshot.size() != 4 || !snapshot.front().keyframe || snapshot[2].pts != 10) {
    std::cerr << "GOP snapshot is incorrect\n";
    return 1;
  }
  if (ring.Append(packet(30, true)) && ring.Snapshot().front().pts != 10) {
    std::cerr << "GOP duration trimming is incorrect\n";
    return 1;
  }
  return 0;
}
