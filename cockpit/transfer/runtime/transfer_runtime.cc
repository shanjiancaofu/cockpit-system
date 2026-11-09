#include "cockpit/transfer/runtime/transfer_runtime.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <memory>
#include <string>

#include "cockpit/exchange/foundation/config/system_config.h"
#include "cockpit/exchange/foundation/logging/logger.h"

namespace cockpit {
namespace transfer {

TransferRuntime::~TransferRuntime() {
  Stop();
}

bool TransferRuntime::Start(const std::string& config_path, int samples, int max_hz) {
  if (running_.load() || worker_.joinable() || max_hz <= 0) {
    return false;
  }

  try {
    const auto system_config = config::SystemConfig::LoadFromFile(config_path);
    const auto& gateway_config = system_config.services().gateway;
    logging::InitLogger(
        "transfer", system_config.paths().log_dir,
        logging::ParseLevel(system_config.logging().level), system_config.logging().mirror_stderr,
        system_config.logging().dump_time_secs, system_config.logging().cut_off_time_mins,
        system_config.logging().max_files);
    if (!gateway_service_.Start(gateway_config.grpc.listen_address, std::max(1, 1000 / max_hz))) {
      return false;
    }
    const auto& rtsp_config = system_config.services().transfer;
    if (rtsp_config.rtsp_enabled || rtsp_config.webrtc_enabled) {
      std::string error;
      if (!video_receiver_.Start(static_cast<std::uint16_t>(rtsp_config.rtsp_udp_port), &error)) {
        LOG_ERROR(error);
        Stop();
        return false;
      }
    }
    if (rtsp_config.webrtc_enabled) {
      WebRtcServerConfig web;
      web.address = rtsp_config.webrtc_address;
      web.http_port = static_cast<std::uint16_t>(rtsp_config.webrtc_http_port);
      web.udp_port_begin = static_cast<std::uint16_t>(rtsp_config.webrtc_udp_port_begin);
      web.udp_port_end = static_cast<std::uint16_t>(rtsp_config.webrtc_udp_port_end);
      web.fps = static_cast<unsigned>(rtsp_config.rtsp_fps);
      web.max_clients = static_cast<unsigned>(rtsp_config.webrtc_max_clients);
      std::string error;
      if (!webrtc_server_.Start(web, video_receiver_, &error)) {
        LOG_ERROR(error);
        Stop();
        return false;
      }
    }
    if (rtsp_config.rtsp_enabled) {
      RtspServerConfig server_config;
      server_config.address = rtsp_config.rtsp_address;
      server_config.port = static_cast<std::uint16_t>(rtsp_config.rtsp_port);
      server_config.path = rtsp_config.rtsp_path;
      server_config.udp_port = static_cast<std::uint16_t>(rtsp_config.rtsp_udp_port);
      server_config.fps = static_cast<std::uint32_t>(rtsp_config.rtsp_fps);
      std::string rtsp_error;
      if (!rtsp_server_.Start(server_config, video_receiver_, &rtsp_error)) {
        LOG_ERROR(rtsp_error);
        Stop();
        return false;
      }
    }
    vehicle_client_ = std::make_unique<gateway::VehicleStateClient>(
        gateway_config.vehicle_data_address, gateway_config.stream_timeout_ms,
        gateway_config.retry_delay_ms);
    LOG_INFO("transfer runtime started grpc_address=" + gateway_config.grpc.listen_address);
  } catch (const std::exception& error) {
    LOG_ERROR("failed to configure transfer: " + std::string(error.what()));
    Stop();
    return false;
  }

  stopping_.store(false);
  result_.store(0);
  running_.store(true);
  worker_ = std::thread([this, samples, max_hz] {
    int stream_result = 0;
    do {
      stream_result = vehicle_client_->Stream(
          samples, max_hz,
          [this](const proto::vehicle::VehicleState& state) {
            gateway_service_.PublishVehicleState(state);
          },
          [this] {
            return !stopping_.load();
          });
      // An unlimited vehicle stream is a background input.  Its failure must
      // not tear down the independently running RTSP publisher; retry after a
      // short delay until Stop() is requested. Finite sample runs retain their
      // existing completion semantics.
      if (samples <= 0 && !stopping_.load() && stream_result != 0) {
        LOG_WARN("vehicle state stream ended; keeping transfer runtime alive");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
    } while (samples <= 0 && !stopping_.load() && stream_result != 0);
    result_.store(stopping_.load() ? 0 : stream_result);
    running_.store(false);
  });
  return true;
}

void TransferRuntime::Stop() {
  stopping_.store(true);
  if (worker_.joinable()) {
    worker_.join();
  }
  gateway_service_.Shutdown();
  webrtc_server_.Stop();
  rtsp_server_.Stop();
  video_receiver_.Stop();
  vehicle_client_.reset();
  running_.store(false);
}

int TransferRuntime::Poll() const {
  if (running_.load()) {
    return 0;
  }
  const int result = result_.load();
  return result == 0 ? 1 : result;
}

}  // namespace transfer
}  // namespace cockpit
