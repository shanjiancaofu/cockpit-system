#include "cockpit/transfer/video/h264_receiver.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <exception>
#include <limits>
#include <utility>

namespace cockpit::transfer {
namespace {
constexpr std::size_t kHeader = 24, kMaxPacket = 8 * 1024 * 1024;
std::uint16_t Get16(const std::uint8_t* p) {
  std::uint16_t v;
  std::memcpy(&v, p, sizeof(v));
  return ntohs(v);
}
std::uint32_t Get32(const std::uint8_t* p) {
  std::uint32_t v;
  std::memcpy(&v, p, sizeof(v));
  return ntohl(v);
}
void AppendNal(std::vector<std::uint8_t>& data, const std::vector<std::uint8_t>& nal) {
  data.insert(data.end(), {0, 0, 0, 1});
  data.insert(data.end(), nal.begin(), nal.end());
}
}  // namespace

std::vector<std::vector<std::uint8_t>> H264Nals(const std::vector<std::uint8_t>& data) {
  std::vector<std::pair<std::size_t, std::size_t>> starts;
  for (std::size_t i = 0; i + 3 <= data.size();) {
    std::size_t n = 0;
    if (data[i] == 0 && data[i + 1] == 0) {
      if (data[i + 2] == 1)
        n = 3;
      else if (i + 4 <= data.size() && data[i + 2] == 0 && data[i + 3] == 1)
        n = 4;
    }
    if (n) {
      starts.emplace_back(i, i + n);
      i += n;
    } else
      ++i;
  }
  std::vector<std::vector<std::uint8_t>> result;
  for (std::size_t i = 0; i < starts.size(); ++i) {
    auto begin = starts[i].second;
    auto end = i + 1 < starts.size() ? starts[i + 1].first : data.size();
    while (end > begin && data[end - 1] == 0) --end;  // Annex-B trailing_zero_8bits
    if (end > begin) result.emplace_back(data.begin() + begin, data.begin() + end);
  }
  return result;
}

std::optional<H264Packet> Crp1Assembler::Feed(const std::uint8_t* p, std::size_t size) {
  const auto now = std::chrono::steady_clock::now();
  if (!parts_.empty() && now - began_ > std::chrono::milliseconds(500)) {
    parts_.clear();
    discontinuity_ = true;
  }
  if (size <= kHeader || Get32(p) != 0x43525031) return {};
  const auto frame = Get32(p + 4);
  const auto index = Get16(p + 8), total = Get16(p + 10);
  const auto timestamp = (static_cast<std::uint64_t>(Get32(p + 12)) << 32) | Get32(p + 16);
  const auto flags = Get32(p + 20);
  if (!total || total > 8192 || index >= total || flags > 1 ||
      timestamp > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    discontinuity_ = true;
    return {};
  }
  if (parts_.empty() || frame != frame_) {
    if (have_last_ && frame == last_) return {};
    if (!parts_.empty() || (have_last_ && frame != last_ + 1U)) discontinuity_ = true;
    parts_.assign(total, {});
    frame_ = frame;
    pts_ = static_cast<std::int64_t>(timestamp);
    key_ = flags == 1;
    bytes_ = received_ = 0;
    began_ = now;
  }
  if (total != parts_.size() || timestamp != static_cast<std::uint64_t>(pts_) ||
      (flags == 1) != key_) {
    parts_.clear();
    discontinuity_ = true;
    return {};
  }
  if (!parts_[index].empty()) return {};
  bytes_ += size - kHeader;
  if (bytes_ > kMaxPacket) {
    parts_.clear();
    discontinuity_ = true;
    return {};
  }
  parts_[index].assign(p + kHeader, p + size);
  if (++received_ != total) return {};
  H264Packet packet;
  packet.pts = packet.dts = pts_;
  packet.keyframe = key_;
  packet.data.reserve(bytes_);
  for (const auto& part : parts_) packet.data.insert(packet.data.end(), part.begin(), part.end());
  parts_.clear();
  have_last_ = true;
  last_ = frame;
  return packet;
}
bool Crp1Assembler::TakeDiscontinuity() {
  return std::exchange(discontinuity_, false);
}

VideoSubscription::VideoSubscription(Consumer consumer, std::size_t capacity)
    : capacity_(std::max<std::size_t>(1, capacity)),
      worker_([this, consumer = std::move(consumer)] {
        for (;;) {
          VideoFramePtr frame;
          {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] {
              return stopping_ || !queue_.empty();
            });
            if (stopping_) return;
            frame = queue_.front();
            queue_.pop_front();
            bytes_ -= frame->packet.data.size();
          }
          // A transport failure must not terminate the receiver or other consumers.
          try {
            consumer(std::move(frame));
          } catch (const std::exception&) {
          }
        }
      }) {
}
VideoSubscription::~VideoSubscription() {
  Stop();
}
void VideoSubscription::Stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
    queue_.clear();
    bytes_ = 0;
  }
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}
void VideoSubscription::Enqueue(VideoFramePtr frame) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (stopping_) return;
  const bool reset = generation_ != frame->generation || frame->discontinuity ||
                     queue_.size() >= capacity_ ||
                     bytes_ + frame->packet.data.size() > 16 * 1024 * 1024;
  if (reset) {
    queue_.clear();
    bytes_ = 0;
    waiting_ = true;
    generation_ = frame->generation;
  }
  if (waiting_) {
    if (!frame->packet.keyframe || frame->sps.size() < 4 || frame->pps.empty()) return;
    // Propagate local queue loss so the downstream RTP/RTSP consumer also resets.
    auto resumed = std::make_shared<VideoFrame>(*frame);
    resumed->discontinuity = true;
    frame = std::move(resumed);
    waiting_ = false;
  }
  bytes_ += frame->packet.data.size();
  queue_.push_back(std::move(frame));
  cv_.notify_one();
}

H264Receiver::~H264Receiver() {
  Stop();
}
bool H264Receiver::Start(std::uint16_t port, std::string* error) {
  if (socket_ >= 0 || !port) return false;
  socket_ = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  timeval timeout{0, 10000};
  if (socket_ < 0 || bind(socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ||
      setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout))) {
    if (error)
      *error = "bind/configure Transfer H264 input failed: " + std::string(std::strerror(errno));
    if (socket_ >= 0) close(socket_);
    socket_ = -1;
    return false;
  }
  const int bytes = 4 * 1024 * 1024;
  setsockopt(socket_, SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes));
  stopping_ = false;
  worker_ = std::thread([this] {
    Receive();
  });
  return true;
}
void H264Receiver::Stop() {
  stopping_ = true;
  if (worker_.joinable()) worker_.join();
  if (socket_ >= 0) close(socket_);
  socket_ = -1;
  std::lock_guard<std::mutex> lock(mutex_);
  status_.available = false;
  last_input_ = {};
}
std::shared_ptr<VideoSubscription> H264Receiver::Subscribe(VideoSubscription::Consumer consumer) {
  auto subscription = std::make_shared<VideoSubscription>(std::move(consumer));
  std::lock_guard<std::mutex> lock(mutex_);
  subscribers_.push_back(subscription);
  return subscription;
}
VideoStatus H264Receiver::Status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto status = status_;
  status.available =
      status.available && std::chrono::steady_clock::now() - last_input_ < std::chrono::seconds(2);
  return status;
}
void H264Receiver::Publish(H264Packet packet, bool discontinuity) {
  const auto nals = H264Nals(packet.data);
  if (nals.empty() || packet.pts < 0 || packet.data.size() > kMaxPacket) return;
  std::lock_guard<std::mutex> lock(mutex_);
  const auto now = std::chrono::steady_clock::now();
  bool restart =
      !status_.available || packet.pts < last_pts_ || now - last_input_ >= std::chrono::seconds(2);
  auto next_sps = status_.sps, next_pps = status_.pps;
  bool idr = false, vcl = false;
  for (const auto& nal : nals) {
    const auto type = nal[0] & 31;
    if (type == 7) next_sps = nal;
    if (type == 8) next_pps = nal;
    if (type == 5) idr = true;
    if (type >= 1 && type <= 5) vcl = true;
  }
  if (!status_.sps.empty() && next_sps != status_.sps) restart = true;
  if (!status_.pps.empty() && next_pps != status_.pps) restart = true;
  if (restart) {
    ++status_.generation;
    // Parameter sets from a previous encoder generation must never be reused.
    status_.sps.clear();
    status_.pps.clear();
    for (const auto& nal : nals) {
      if ((nal[0] & 31) == 7) status_.sps = nal;
      if ((nal[0] & 31) == 8) status_.pps = nal;
    }
  } else {
    status_.sps = std::move(next_sps);
    status_.pps = std::move(next_pps);
  }
  last_pts_ = packet.pts;
  last_input_ = now;
  status_.available = true;
  if (!vcl) return;
  packet.keyframe = idr;  // Validate actual NAL type, never trust only the wire flag.
  packet.data.clear();
  if (idr && !status_.sps.empty() && !status_.pps.empty()) {
    AppendNal(packet.data, status_.sps);
    AppendNal(packet.data, status_.pps);
  }
  for (const auto& nal : nals)
    if ((nal[0] & 31) != 7 && (nal[0] & 31) != 8) AppendNal(packet.data, nal);
  auto frame = std::make_shared<VideoFrame>();
  frame->packet = std::move(packet);
  frame->generation = status_.generation;
  frame->discontinuity = discontinuity || restart;
  frame->sps = status_.sps;
  frame->pps = status_.pps;
  for (auto it = subscribers_.begin(); it != subscribers_.end();) {
    if (auto subscriber = it->lock()) {
      subscriber->Enqueue(frame);
      ++it;
    } else
      it = subscribers_.erase(it);
  }
}
void H264Receiver::Receive() {
  Crp1Assembler assembler;
  std::vector<std::uint8_t> data(65536);
  H264Packet pending;
  bool discontinuity = false;
  auto flush = [&] {
    if (!pending.data.empty()) {
      Publish(std::move(pending), discontinuity);
      pending = {};
      discontinuity = false;
    }
  };
  while (!stopping_) {
    const auto size = recv(socket_, data.data(), data.size(), 0);
    if (size <= 0) {
      flush();
      continue;
    }
    auto packet = assembler.Feed(data.data(), static_cast<std::size_t>(size));
    if (!packet) continue;
    const bool lost = assembler.TakeDiscontinuity();
    if (!pending.data.empty() && (packet->pts != pending.pts || lost)) flush();
    if (pending.data.size() + packet->data.size() > kMaxPacket) {
      pending = {};
      discontinuity = true;
      continue;
    }
    pending.pts = pending.dts = packet->pts;
    pending.keyframe = pending.keyframe || packet->keyframe;
    pending.data.insert(pending.data.end(), packet->data.begin(), packet->data.end());
    discontinuity = discontinuity || lost;
  }
}
}  // namespace cockpit::transfer
