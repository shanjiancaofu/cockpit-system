#include "cockpit/modules/can/socket_can_adapter.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

bool Expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  bool success = true;
  for (const std::uint8_t length : {8U, 12U, 16U, 20U, 32U, 48U, 64U}) {
    cockpit::can::SocketCanFrame source;
    source.id = length == 64U ? 0x1ABCDEU : 0x321U;
    source.length = length;
    source.extended = length == 64U;
    source.fd = length > 8U;
    source.brs = source.fd;
    source.esi = source.fd && length == 48U;
    for (std::size_t index = 0; index < length; ++index) {
      source.data[index] = static_cast<std::uint8_t>((index * 3U + length) & 0xFFU);
    }

    cockpit::can::CanFrame domain;
    std::string error;
    success &= Expect(cockpit::can::FromSocketCanFrame(source, &domain, &error),
                      "SocketCAN frame conversion failed");
    const auto round_trip = cockpit::can::ToSocketCanFrame(domain);
    success &= Expect(round_trip.IsValidForSend(), "round-trip frame is invalid");
    success &= Expect(round_trip.id == source.id && round_trip.length == source.length &&
                          round_trip.extended == source.extended && round_trip.fd == source.fd &&
                          round_trip.brs == source.brs && round_trip.esi == source.esi &&
                          round_trip.data == source.data,
                      "CAN FD frame data or flags changed during conversion");
  }

  cockpit::can::SocketCanFrame invalid_fd_remote;
  invalid_fd_remote.id = 0x123U;
  invalid_fd_remote.fd = true;
  invalid_fd_remote.remote = true;
  success &= Expect(!invalid_fd_remote.IsValidForSend(), "CAN FD RTR frame was accepted");

  cockpit::can::SocketCanFrame oversized_classic;
  oversized_classic.id = 0x123U;
  oversized_classic.length = 12U;
  success &= Expect(!oversized_classic.IsValidForSend(), "oversized Classical CAN was accepted");

  cockpit::can::SocketCanFrame error_frame;
  error_frame.error = true;
  error_frame.error_mask = 0x40U;
  cockpit::can::CanFrame ignored;
  std::string error;
  success &= Expect(!cockpit::can::FromSocketCanFrame(error_frame, &ignored, &error) &&
                        error.find("error frame") != std::string::npos,
                    "SocketCAN error frame was treated as data");
  return success ? 0 : 1;
}
