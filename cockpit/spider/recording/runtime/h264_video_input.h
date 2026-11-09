#pragma once

#include <atomic>
#include <functional>
#include <thread>

#include "cockpit/spider/recording/video/h264_gop_ring.h"

namespace cockpit::recording {

// CRP1 is the local encoded-packet protocol also consumed by RTSP. No camera
// implementation or RTSP process is needed by this receiver. PTS units: frames,
// B-frames disabled (DTS == PTS); fps is configured by the recording service.
class H264VideoInput final {
 public:
  using Consumer = std::function<void(const EncodedVideoPacket&, bool discontinuity)>;
  ~H264VideoInput();
  bool Start(std::uint16_t port, Consumer consumer, std::function<void()> tick,
             std::string* error);
  void Stop();

 private:
  void Receive(Consumer consumer, std::function<void()> tick);
  int socket_ = -1;
  std::atomic_bool stopping_{false};
  std::thread worker_;
};

}  // namespace cockpit::recording
