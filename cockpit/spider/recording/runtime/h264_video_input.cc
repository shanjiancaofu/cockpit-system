#include "cockpit/spider/recording/runtime/h264_video_input.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <cerrno>
#include <string>
#include <limits>
#include <vector>

namespace cockpit::recording {
namespace {
constexpr std::size_t kHeader = 24;
constexpr std::size_t kMaxPacket = 8 * 1024 * 1024;
constexpr std::size_t kMaxFragments = 8192;
std::uint16_t Get16(const std::uint8_t* p) {
  std::uint16_t value;
  std::memcpy(&value, p, sizeof(value));
  return ntohs(value);
}
std::uint32_t Get32(const std::uint8_t* p) {
  std::uint32_t value;
  std::memcpy(&value, p, sizeof(value));
  return ntohl(value);
}
std::uint64_t Get64(const std::uint8_t* p) {
  return (static_cast<std::uint64_t>(Get32(p)) << 32) | Get32(p + 4);
}
}  // namespace

H264VideoInput::~H264VideoInput() { Stop(); }

bool H264VideoInput::Start(std::uint16_t port, Consumer consumer, std::function<void()> tick,
                           std::string* error) {
  if (socket_ >= 0 || port == 0 || !consumer || !tick) {
    if (error) *error = "invalid recording video input configuration";
    return false;
  }
  socket_ = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  if (socket_ < 0 || bind(socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    if (error) *error = "bind recording video input failed: " + std::string(std::strerror(errno));
    if (socket_ >= 0) close(socket_);
    socket_ = -1;
    return false;
  }
  timeval timeout{0, 100000};
  const int receive_bytes = 4 * 1024 * 1024;
  if (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
    if (error) *error = "configure recording video input timeout failed";
    close(socket_);
    socket_ = -1;
    return false;
  }
  setsockopt(socket_, SOL_SOCKET, SO_RCVBUF, &receive_bytes, sizeof(receive_bytes));
  stopping_ = false;
  worker_ = std::thread([this, consumer = std::move(consumer), tick = std::move(tick)] {
    Receive(consumer, tick);
  });
  return true;
}

void H264VideoInput::Stop() {
  stopping_ = true;
  if (worker_.joinable()) worker_.join();
  if (socket_ >= 0) close(socket_);
  socket_ = -1;
}

void H264VideoInput::Receive(Consumer consumer, std::function<void()> tick) {
  std::vector<std::uint8_t> datagram(65536);
  std::vector<std::vector<std::uint8_t>> fragments;
  std::uint32_t frame = 0, last_frame = 0;
  std::int64_t pts = 0;
  bool keyframe = false, have_last = false, discontinuity = true;
  std::size_t bytes = 0, received = 0;
  auto began = std::chrono::steady_clock::now();
  while (!stopping_) {
    const auto count = recv(socket_, datagram.data(), datagram.size(), 0);
    tick();
    if (std::chrono::steady_clock::now() - began > std::chrono::milliseconds(500)) {
      if (!fragments.empty()) discontinuity = true;
      fragments.clear();
    }
    if (count <= static_cast<ssize_t>(kHeader) || Get32(datagram.data()) != 0x43525031) continue;
    const auto current = Get32(datagram.data() + 4);
    const auto index = Get16(datagram.data() + 8);
    const auto total = Get16(datagram.data() + 10);
    const auto timestamp = Get64(datagram.data() + 12);
    const auto flags = Get32(datagram.data() + 20);
    if (total == 0 || total > kMaxFragments || index >= total || flags > 1 ||
        timestamp > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) continue;
    if (fragments.empty() || current != frame) {
      if (!fragments.empty() || (have_last && current != last_frame + 1U)) discontinuity = true;
      if (have_last && current == last_frame) continue; // duplicate completed packet
      frame = current;
      pts = static_cast<std::int64_t>(timestamp);
      keyframe = flags == 1;
      fragments.assign(total, {});
      received = bytes = 0;
      began = std::chrono::steady_clock::now();
    }
    if (total != fragments.size() || timestamp != static_cast<std::uint64_t>(pts) ||
        (flags == 1) != keyframe) {
      discontinuity = true;
      fragments.clear();
      continue;
    }
    if (!fragments[index].empty()) continue;
    bytes += static_cast<std::size_t>(count) - kHeader;
    if (bytes > kMaxPacket) {
      fragments.clear();
      discontinuity = true;
      continue;
    }
    fragments[index].assign(datagram.begin() + kHeader, datagram.begin() + count);
    if (++received != total) continue;
    EncodedVideoPacket packet;
    packet.pts = packet.dts = pts;
    packet.keyframe = keyframe;
    packet.data.reserve(bytes);
    for (const auto& part : fragments) packet.data.insert(packet.data.end(), part.begin(), part.end());
    consumer(packet, discontinuity);
    have_last = true;
    last_frame = frame;
    discontinuity = false;
    fragments.clear();
  }
}
}  // namespace cockpit::recording
