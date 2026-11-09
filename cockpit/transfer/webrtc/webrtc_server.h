#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "cockpit/transfer/video/h264_receiver.h"

namespace cockpit::transfer {
struct WebRtcServerConfig {
  std::string address = "127.0.0.1";
  std::uint16_t http_port = 8080;
  std::uint16_t udp_port_begin = 50000;
  std::uint16_t udp_port_end = 50010;
  unsigned fps = 30;
  unsigned max_clients = 1;
};
class WebRtcServer {
 public:
  WebRtcServer();
  ~WebRtcServer();
  WebRtcServer(const WebRtcServer&) = delete;
  WebRtcServer& operator=(const WebRtcServer&) = delete;
  bool Start(const WebRtcServerConfig& config, H264Receiver& receiver, std::string* error = nullptr,
             std::function<void()> request_idr = {});
  void Stop();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace cockpit::transfer
