#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "cockpit/driver/camera/encode/h264_encoder.h"

namespace {
using namespace cockpit::camera;
void Check(bool ok, const std::string& error) {
  if (!ok) throw std::runtime_error(error);
}
// Verify actual Annex-B NAL types, not just AV_PKT_FLAG_KEY or request counters.
bool Nal(const H264Packet& packet, unsigned type) {
  const auto& d = packet.data;
  for (std::size_t i = 0; i + 3 < d.size(); ++i) {
    if (d[i] || d[i + 1]) continue;
    const auto prefix =
        d[i + 2] == 1 ? 3U : (i + 4 < d.size() && !d[i + 2] && d[i + 3] == 1 ? 4U : 0U);
    if (prefix && (d[i + prefix] & 31) == type) return true;
  }
  return false;
}
}  // namespace
int main() {
  if (!H264Encoder::IsSupported()) {
    std::cerr << "H264 encoder unavailable\n";
    return 77;
  }
  try {
    H264Encoder encoder;
    std::string error;
    Check(!encoder.RequestIdr(&error), "stopped encoder accepted IDR");
    H264EncoderConfig config;
    config.width = 128;
    config.height = 72;
    config.gop = 300;
    Check(encoder.Start(config, &error), error);
    CpuVideoFrame input;
    input.format = VicOutputFormat::kI420;
    input.width = 128;
    input.height = 72;
    input.planes = {{0, 128, 128, 72}, {128 * 72, 64, 64, 36}, {128 * 72 + 64 * 36, 64, 64, 36}};
    input.data.resize(128 * 72 * 3 / 2, 128);
    auto encode = [&](unsigned id) {
      input.capture_id = id;
      std::vector<H264Packet> packets;
      Check(encoder.Encode(input, &packets, &error), error);
      Check(packets.size() == 1, "zerolatency encoder did not produce exactly one AU");
      return packets.front();
    };
    Check(Nal(encode(0), 5), "first frame has no IDR");
    for (unsigned i = 1; i < 5; ++i) Check(!Nal(encode(i), 5), "unexpected periodic IDR");
    for (unsigned i = 0; i < 100; ++i) Check(encoder.RequestIdr(&error), error);
    const auto forced = encode(5);
    Check(Nal(forced, 5) && Nal(forced, 7) && Nal(forced, 8), "requested frame lacks IDR/SPS/PPS");
    Check(encoder.stats().idr_requests == 100 && encoder.stats().forced_idr_frames == 1,
          "burst requests did not coalesce");
    Check(!Nal(encode(6), 5), "forcing IDR leaked into following frame");
    Check(encoder.RequestIdr(&error), error);
    // Invalid input must not consume a pending request.
    auto invalid = input;
    invalid.planes.clear();
    std::vector<H264Packet> discarded;
    Check(!encoder.Encode(invalid, &discarded, &error), "invalid I420 accepted");
    Check(!Nal(encode(7), 5) && encoder.stats().forced_idr_frames == 1,
          "forced IDR rate limit bypassed");
    std::this_thread::sleep_for(std::chrono::milliseconds(260));
    Check(Nal(encode(8), 5) && encoder.stats().forced_idr_frames == 2, "pending IDR not emitted");
    std::vector<H264Packet> flushed;
    Check(encoder.Flush(&flushed, &error), error);
    Check(!encoder.RequestIdr(&error), "drained encoder accepted IDR");
    encoder.Stop();
    Check(!encoder.RequestIdr(&error), "stopped encoder accepted IDR");
    Check(encoder.Start(config, &error), error);
    Check(encoder.stats().idr_requests == 0 && encoder.stats().forced_idr_frames == 0,
          "restart retained requests");
    encode(0);
    Check(!Nal(encode(1), 5), "restart retained pending IDR");
    std::cout << "forced NAL type 5 + SPS/PPS, coalescing and lifecycle passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
