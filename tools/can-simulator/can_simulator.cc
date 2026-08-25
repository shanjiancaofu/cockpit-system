#include "can_simulator.h"

#include <array>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "cockpit/core/logging/logger.h"
#include "cockpit/core/runtime/process_runtime.h"
#include "cockpit/drivers/socketcan/socket_can.h"
#include "cockpit/modules/can/can_frame.h"
#include "cockpit/modules/can/socket_can_adapter.h"
#include "cockpit/modules/vehicle/chassis_can_codec.h"
#include "cockpit/modules/vehicle/vehicle_can_codec.h"
#include "cockpit/modules/vehicle/vehicle_state.h"

namespace {

bool SendFrame(const std::string& backend, const cockpit::can::CanFrame& frame,
               const cockpit::can::SocketCan& socket, const std::string& interface_name) {
  if (backend == "socketcan") {
    std::string error;
    if (!socket.Send(cockpit::can::ToSocketCanFrame(frame), &error)) {
      LOG_ERROR(error);
      return false;
    }
  }
  std::cout << interface_name << ' ' << frame.ToString() << std::endl;
  return true;
}

bool WaitForHandshake(const cockpit::can::SocketCan& socket, int timeout_ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    cockpit::can::SocketCanFrame received;
    std::string error;
    const auto status = socket.Receive(&received, 20, &error);
    if (status == cockpit::can::CanIoStatus::kTimeout) {
      continue;
    }
    if (status != cockpit::can::CanIoStatus::kOk) {
      LOG_ERROR(error.empty() ? "failed to receive chassis handshake" : error);
      return false;
    }
    cockpit::can::CanFrame frame;
    if (cockpit::can::FromSocketCanFrame(received, &frame, &error) &&
        cockpit::vehicle::ChassisCanCodec::DecodeHandshakeResponse(frame)) {
      return true;
    }
  }
  LOG_ERROR("chassis handshake response timed out");
  return false;
}

std::int64_t SteadyNowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void DecodeChassisFrame(const cockpit::can::CanFrame& frame,
                        cockpit::vehicle::ChassisHeartbeatMonitor* heartbeat_monitor,
                        std::int64_t now_ms) {
  using cockpit::vehicle::ChassisCanCodec;
  cockpit::vehicle::ChassisMotionStatus motion;
  cockpit::vehicle::ChassisOdometryReport odometry;
  cockpit::vehicle::ChassisHeartbeat heartbeat;
  cockpit::vehicle::ChassisFaultStatus fault;

  if (ChassisCanCodec::DecodeMotion(frame, &motion)) {
    std::cout << "chassis motion seq=" << static_cast<int>(motion.sequence)
              << " running=" << (motion.running ? "true" : "false")
              << " linear_mm_s=" << motion.linear_velocity_mm_s
              << " angular_mrad_s=" << motion.angular_velocity_mrad_s << std::endl;
  } else if (ChassisCanCodec::DecodeOdometry(frame, &odometry)) {
    std::cout << "chassis odometry seq=" << static_cast<int>(odometry.sequence)
              << " x_mm=" << odometry.x_mm << " y_mm=" << odometry.y_mm
              << " heading_mrad=" << odometry.heading_mrad << std::endl;
  } else if (heartbeat_monitor != nullptr && heartbeat_monitor->Process(frame, now_ms) &&
             ChassisCanCodec::DecodeHeartbeat(frame, &heartbeat)) {
    std::cout << "chassis heartbeat seq=" << static_cast<int>(heartbeat.sequence)
              << " state=" << static_cast<int>(heartbeat.node_state)
              << " uptime_ms=" << heartbeat.uptime_ms << " faults=0x" << std::hex
              << heartbeat.fault_summary << std::dec << std::endl;
  } else if (ChassisCanCodec::DecodeFault(frame, &fault)) {
    std::cout << "chassis fault seq=" << static_cast<int>(fault.sequence)
              << " severity=" << static_cast<int>(fault.severity) << " active=0x" << std::hex
              << fault.active_faults << " latched=0x" << fault.latched_faults << std::dec
              << " fault_sequence=" << fault.fault_sequence << std::endl;
  }
}

int SimulateChassis(const cockpit::runtime::ProcessRuntime& runtime, const std::string& backend,
                    const std::string& interface_name, const cockpit::can::SocketCan& socket) {
  using cockpit::vehicle::ChassisCanCodec;
  const int samples = runtime.args().GetInt("samples", 10);
  const int interval_ms = runtime.args().GetInt("interval-ms", 20);
  const int timeout_ms = runtime.args().GetInt("timeout-ms", 1000);
  const int linear_mm_s = runtime.args().GetInt("linear-mm-s", 0);
  const int angular_mrad_s = runtime.args().GetInt("angular-mrad-s", 0);
  const int initial_sequence = runtime.args().GetInt("sequence", 1);
  const bool development_handshake = runtime.args().HasFlag("development-handshake");
  if (samples < 0 || interval_ms <= 0 || timeout_ms <= 0 || initial_sequence < 0 ||
      initial_sequence > 255 || linear_mm_s < -2000 || linear_mm_s > 2000 ||
      angular_mrad_s < -10000 || angular_mrad_s > 10000) {
    LOG_ERROR("invalid chassis simulator arguments");
    return 2;
  }

  if (development_handshake) {
    if (!SendFrame(backend, ChassisCanCodec::EncodeHandshakePing(), socket, interface_name)) {
      return 1;
    }
    if (backend == "socketcan" && !WaitForHandshake(socket, timeout_ms)) {
      return 1;
    }
    if (!SendFrame(backend, ChassisCanCodec::EncodeHandshakePass(), socket, interface_name)) {
      return 1;
    }
  }

  std::uint8_t sequence = static_cast<std::uint8_t>(initial_sequence);
  std::uint8_t heartbeat_sequence = 0U;
  const std::int64_t started_ms = SteadyNowMs();
  std::int64_t heartbeat_due_ms = started_ms;
  cockpit::vehicle::ChassisHeartbeatMonitor heartbeat_monitor;
  for (int index = 0; index < samples && !runtime.ShouldStop(); ++index) {
    const std::int64_t now_ms = SteadyNowMs();
    if (now_ms >= heartbeat_due_ms) {
      cockpit::can::CanFrame heartbeat;
      if (!ChassisCanCodec::EncodeHeartbeat(
              cockpit::vehicle::ChassisHeartbeat{
                  2U, heartbeat_sequence, 1U, static_cast<std::uint32_t>(now_ms - started_ms), 0U},
              &heartbeat) ||
          !SendFrame(backend, heartbeat, socket, interface_name)) {
        return 1;
      }
      ++heartbeat_sequence;
      heartbeat_due_ms = now_ms + 100;
    }
    cockpit::can::CanFrame command;
    if (!ChassisCanCodec::EncodeVelocity(
            cockpit::vehicle::ChassisVelocityCommand{true, sequence,
                                                     static_cast<std::int16_t>(linear_mm_s),
                                                     static_cast<std::int16_t>(angular_mrad_s)},
            &command) ||
        !SendFrame(backend, command, socket, interface_name)) {
      return 1;
    }
    ++sequence;
    if (backend == "socketcan") {
      const auto deadline =
          std::chrono::steady_clock::now() + std::chrono::milliseconds(interval_ms);
      while (std::chrono::steady_clock::now() < deadline) {
        cockpit::can::SocketCanFrame received;
        std::string error;
        const auto status = socket.Receive(&received, 1, &error);
        if (status == cockpit::can::CanIoStatus::kTimeout) {
          continue;
        }
        if (status != cockpit::can::CanIoStatus::kOk) {
          LOG_ERROR(error.empty() ? "failed to receive chassis status" : error);
          return 1;
        }
        cockpit::can::CanFrame frame;
        if (cockpit::can::FromSocketCanFrame(received, &frame, &error)) {
          DecodeChassisFrame(frame, &heartbeat_monitor, SteadyNowMs());
        }
      }
      heartbeat_monitor.Update(SteadyNowMs());
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
  }

  cockpit::can::CanFrame stop;
  if (!ChassisCanCodec::EncodeVelocity(
          cockpit::vehicle::ChassisVelocityCommand{false, sequence, 0, 0}, &stop) ||
      !SendFrame(backend, stop, socket, interface_name)) {
    return 1;
  }
  const auto peer = heartbeat_monitor.GetSnapshot(SteadyNowMs());
  if (backend == "socketcan") {
    const char* status = "unknown";
    if (peer.status == cockpit::vehicle::ChassisHeartbeatStatus::kAlive) {
      status = "alive";
    } else if (peer.status == cockpit::vehicle::ChassisHeartbeatStatus::kTimeout) {
      status = "timeout";
    }
    std::cout << "chassis peer-heartbeat status=" << status << " age_ms=" << peer.age_ms
              << std::endl;
  }
  return 0;
}

}  // namespace

int cockpit::can_simulator::SimulateCan(const cockpit::runtime::ProcessRuntime& runtime) {
  const auto& config = runtime.config().hardware().can;

  const std::string can_if = runtime.args().GetString("interface", config.interface);
  const std::string backend = runtime.args().GetString("backend", config.simulator_backend);
  const int interval_ms = config.simulator_interval_ms;
  const int samples = runtime.args().GetInt("samples", 10);
  const std::string protocol = runtime.args().GetString("protocol", "prototype");
  const int fd_payload_size = runtime.args().GetInt("fd-payload-size", 0);
  if (fd_payload_size < 0 || fd_payload_size > 64) {
    LOG_ERROR("fd-payload-size must be between 0 and 64");
    return 2;
  }

  cockpit::can::SocketCan socket;
  if (backend == "socketcan") {
    std::string error;
    if (!socket.Open(can_if, &error)) {
      LOG_ERROR(error);
      return 1;
    }
  } else if (backend != "stdout") {
    LOG_ERROR("unsupported CAN simulator backend: " + backend);
    return 2;
  }

  LOG_INFO("can-simulator started interface=" + can_if + " backend=" + backend);
  if (protocol == "chassis") {
    return SimulateChassis(runtime, backend, can_if, socket);
  }
  if (protocol != "prototype") {
    LOG_ERROR("unsupported CAN simulator protocol: " + protocol);
    return 2;
  }
  if (fd_payload_size > 0) {
    cockpit::can::SocketCanFrame diagnostic;
    diagnostic.id = 0x700U;
    diagnostic.fd = true;
    diagnostic.brs = runtime.args().HasFlag("brs");
    diagnostic.extended = runtime.args().HasFlag("extended");
    if (diagnostic.extended) {
      diagnostic.id = 0x18DAF110U;
    }
    diagnostic.length = static_cast<std::uint8_t>(fd_payload_size);
    for (int index = 0; index < fd_payload_size; ++index) {
      diagnostic.data[static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(index);
    }
    if (backend == "socketcan") {
      std::string error;
      if (!socket.Send(diagnostic, &error)) {
        LOG_ERROR(error);
        return 1;
      }
    }
    std::cout << can_if << " diagnostic-canfd id=" << diagnostic.id << " length=" << fd_payload_size
              << " brs=" << (diagnostic.brs ? "true" : "false") << std::endl;
  }
  for (int i = 0; i < samples && !runtime.ShouldStop(); ++i) {
    const auto frame =
        cockpit::vehicle::VehicleCanCodec::Encode(cockpit::vehicle::MakeMockVehicleState(i));
    if (backend == "socketcan") {
      std::string error;
      if (!socket.Send(cockpit::can::ToSocketCanFrame(frame), &error)) {
        LOG_ERROR(error);
        return 1;
      }
    }
    LOG_DEBUG("emit CAN " + can_if + " " + frame.ToString());
    std::cout << can_if << ' ' << frame.ToString() << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
  }
  return 0;
}
