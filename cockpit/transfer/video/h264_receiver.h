#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace cockpit::transfer {
// Transfer-side wire value. Keep this independent of Camera implementation headers.
struct H264Packet {
  std::vector<std::uint8_t> data;
  std::int64_t pts = 0, dts = 0;
  bool keyframe = false;
};

// CRP1 timestamps use encoder frame ticks (1 / configured FPS), not microseconds.
struct VideoFrame {
  H264Packet packet;
  std::uint64_t generation = 0;
  bool discontinuity = false;
  std::vector<std::uint8_t> sps, pps;
};
using VideoFramePtr = std::shared_ptr<const VideoFrame>;
std::vector<std::vector<std::uint8_t>> H264Nals(const std::vector<std::uint8_t>& data);

class Crp1Assembler {
 public:
  std::optional<H264Packet> Feed(const std::uint8_t* data, std::size_t size);
  bool TakeDiscontinuity();

 private:
  std::vector<std::vector<std::uint8_t>> parts_;
  std::uint32_t frame_ = 0, last_ = 0;
  std::int64_t pts_ = 0;
  bool key_ = false, have_last_ = false, discontinuity_ = true;
  std::size_t bytes_ = 0, received_ = 0;
  std::chrono::steady_clock::time_point began_{};
};

// One worker and bounded queue per consumer. Overflow discards the entire queued
// GOP and resumes only at a complete IDR with the current SPS/PPS.
class VideoSubscription {
 public:
  using Consumer = std::function<void(VideoFramePtr)>;
  explicit VideoSubscription(Consumer consumer, std::size_t capacity = 8,
                             std::function<void()> request_idr = {});
  ~VideoSubscription();
  void Stop();
  void Enqueue(VideoFramePtr frame);

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<VideoFramePtr> queue_;
  std::size_t capacity_, bytes_ = 0;
  std::function<void()> request_idr_;
  bool stopping_ = false, waiting_ = true;
  std::uint64_t generation_ = 0;
  std::thread worker_;
};

struct VideoStatus {
  std::uint64_t generation = 0;
  bool available = false;
  bool has_idr = false;
  std::int64_t input_age_ms = -1;
  std::vector<std::uint8_t> sps, pps;
};
class H264Receiver {
 public:
  ~H264Receiver();
  bool Start(std::uint16_t port, std::string* error = nullptr);
  void Stop();
  std::shared_ptr<VideoSubscription> Subscribe(VideoSubscription::Consumer consumer,
                                               std::function<void()> request_idr = {});
  VideoStatus Status() const;
  // Also used by the protocol and media integration tests.
  void Publish(H264Packet packet, bool discontinuity);

 private:
  void Receive();
  mutable std::mutex mutex_;
  std::vector<std::weak_ptr<VideoSubscription>> subscribers_;
  VideoStatus status_;
  std::chrono::steady_clock::time_point last_input_{};
  std::int64_t last_pts_ = 0;
  int socket_ = -1;
  std::atomic_bool stopping_{false};
  std::thread worker_;
};
}  // namespace cockpit::transfer
