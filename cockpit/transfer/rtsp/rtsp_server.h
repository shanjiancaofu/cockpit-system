#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace cockpit::transfer {

struct RtspServerConfig {
  std::string address = "0.0.0.0";
  std::uint16_t port = 8554;
  std::string path = "/camera";
  std::uint16_t udp_port = 8556;
  std::uint32_t fps = 30;
};

class RtspServer final {
 public:
  RtspServer();
  ~RtspServer();
  RtspServer(const RtspServer&) = delete;
  RtspServer& operator=(const RtspServer&) = delete;

  bool Start(const RtspServerConfig& config, std::string* error = nullptr);
  void Stop();
  bool running() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace cockpit::transfer
