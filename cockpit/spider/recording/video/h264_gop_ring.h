#pragma once

#include <cstdint>
#include <deque>
#include <vector>

namespace cockpit::recording {

struct EncodedVideoPacket {
  std::vector<std::uint8_t> data;
  std::int64_t pts = 0;
  std::int64_t dts = 0;
  bool keyframe = false;
};

class H264GopRing final {
 public:
  explicit H264GopRing(std::int64_t max_duration_ms = 10'000, std::uint32_t fps = 30);

  bool Append(const EncodedVideoPacket& packet);
  std::vector<EncodedVideoPacket> Snapshot() const;
  void Clear();
  std::size_t size() const;

 private:
  using Gop = std::vector<EncodedVideoPacket>;
  void Trim();

  const std::int64_t max_duration_pts_;
  std::deque<Gop> gops_;
  Gop current_;
};

}  // namespace cockpit::recording
