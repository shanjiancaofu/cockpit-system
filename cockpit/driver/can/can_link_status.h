#pragma once

#include <cstdint>
#include <string>

namespace cockpit {
namespace can {

enum class CanCommunicationState {
  kStarting,
  kOnline,
  kIdle,
  kFaulted,
};

struct CanLinkStatus {
  std::string interface_name;
  bool fd_enabled = false;
  CanCommunicationState state = CanCommunicationState::kStarting;
  std::uint64_t last_rx_timestamp_ms = 0;
  std::uint64_t rx_frames = 0;
  std::uint64_t decoded_frames = 0;
  std::uint64_t invalid_frames = 0;
  std::uint64_t idle_timeouts = 0;
  std::uint64_t error_frames = 0;
  std::uint64_t bus_off_count = 0;
  std::uint64_t error_passive_count = 0;
  std::uint64_t error_warning_count = 0;
  std::uint64_t ack_error_count = 0;
  std::uint64_t protocol_error_count = 0;
  std::string last_error;
};

}  // namespace can
}  // namespace cockpit
