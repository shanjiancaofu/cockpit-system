#include "cockpit/spider/recording/video/h264_gop_ring.h"

#include <cstdint>
#include <cstdlib>
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
  const auto require = [](bool ok, const char* message) {
    if (!ok) {
      std::cerr << message << '\n';
      std::exit(1);
    }
  };
  H264GopRing bounded(10000, 30, 15);
  require(bounded.Append(packet(0, true)) && bounded.Append(packet(1, false)) &&
              bounded.Append(packet(2, true)) && bounded.Append(packet(3, false)),
          "byte-budget append failed");
  require(bounded.bytes() == 10 && bounded.size() == 2 && bounded.start_pts() == 2 &&
              bounded.Snapshot().front().keyframe,
          "byte trimming must remove a whole old GOP");
  require(bounded.Append(packet(4, false)) && !bounded.Append(packet(5, false)),
          "oversized current GOP not rejected");
  require(bounded.bytes() == 0 && bounded.size() == 0 && bounded.start_pts() == -1 &&
              bounded.dropped_gops() == 1 && !bounded.Append(packet(6, false)),
          "overflow must reset and wait for IDR");
  require(bounded.Append(packet(7, true)), "IDR did not recover bounded ring");
  auto oversized = packet(8, true);
  oversized.data.resize(16);
  require(!bounded.Append(oversized) && bounded.bytes() == 0 && bounded.dropped_gops() == 2,
          "oversized IDR must not be copied into ring");
  H264GopRing no_idr(1000, 10);
  require(no_idr.Append(packet(0, true)) && no_idr.Append(packet(10, false)) &&
              !no_idr.Append(packet(11, false)) && no_idr.size() == 0 &&
              no_idr.dropped_gops() == 1 && !no_idr.Append(packet(12, false)) &&
              no_idr.Append(packet(13, true)),
          "missing IDR duration limit or recovery failed");
  H264GopRing stress(2000, 30, 150);
  for (int pts = 0; pts < 10000; ++pts) {
    require(stress.Append(packet(pts, pts % 10 == 0)), "normal GOP unexpectedly dropped");
    const auto view = stress.Snapshot();
    std::size_t bytes = 0;
    for (const auto& item : view) bytes += item.data.size();
    require(view.size() == stress.size() && bytes == stress.bytes() && bytes <= 150 &&
                !view.empty() && view.front().keyframe && stress.start_pts() == view.front().pts,
            "cached counters or IDR boundary inconsistent");
  }
  stress.Clear();
  require(stress.bytes() == 0 && stress.size() == 0 && stress.start_pts() == -1,
          "Clear did not reset counters");
  return 0;
}
