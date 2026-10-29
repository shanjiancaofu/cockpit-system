#include "cockpit/transfer/rtsp/rtsp_server.h"

namespace cockpit::transfer {
struct RtspServer::Impl {};
RtspServer::RtspServer() : impl_(std::make_unique<Impl>()) {
}
RtspServer::~RtspServer() = default;
bool RtspServer::Start(const RtspServerConfig&, std::string* error) {
  if (error) *error = "GStreamer RTSP Server dependency is unavailable";
  return false;
}
void RtspServer::Stop() {
}
bool RtspServer::running() const {
  return false;
}
}  // namespace cockpit::transfer
