#include "cockpit/transfer/webrtc/webrtc_server.h"

namespace cockpit::transfer {
struct WebRtcServer::Impl {};
WebRtcServer::WebRtcServer() : impl_(std::make_unique<Impl>()) {
}
WebRtcServer::~WebRtcServer() = default;
bool WebRtcServer::Start(const WebRtcServerConfig&, H264Receiver&, std::string* error,
                         std::function<void()>) {
  if (error) *error = "WebRTC was not built; configure with COCKPIT_ENABLE_WEBRTC=ON";
  return false;
}
void WebRtcServer::Stop() {
}
}  // namespace cockpit::transfer
