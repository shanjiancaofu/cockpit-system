#pragma once

#include <cstddef>
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
  explicit H264GopRing(std::int64_t max_duration_ms = 10'000, std::uint32_t fps = 30,
                       std::size_t max_bytes = 64 * 1024 * 1024);

  bool Append(const EncodedVideoPacket& packet);
  std::vector<EncodedVideoPacket> Snapshot() const;
  void Clear();
  std::size_t size() const;
  std::size_t bytes() const {
    return bytes_;
  }
  std::int64_t start_pts() const;
  // Incremented when an oversized/overlong GOP must be discarded. Wait for IDR.
  std::uint64_t dropped_gops() const {
    return dropped_gops_;
  }

 private:
  using Gop = std::vector<EncodedVideoPacket>;
  void Trim(std::int64_t newest, std::size_t incoming_bytes);

  const std::int64_t max_duration_pts_;
  const std::size_t max_bytes_;
  std::size_t bytes_ = 0, current_bytes_ = 0, packets_ = 0;
  std::uint64_t dropped_gops_ = 0;
  std::deque<Gop> gops_;
  Gop current_;
};

}  // namespace cockpit::recording
