#include "cockpit/modules/can/socket_can_adapter.h"

#include <algorithm>

namespace cockpit {
namespace can {

SocketCanFrame ToSocketCanFrame(const CanFrame& frame) {
  SocketCanFrame result;
  result.id = frame.id();
  result.length = frame.data_length();
  result.extended = frame.extended();
  result.remote = frame.remote();
  result.fd = frame.fd();
  result.brs = frame.brs();
  result.esi = frame.esi();
  std::copy_n(frame.data().begin(), result.length, result.data.begin());
  return result;
}

bool FromSocketCanFrame(const SocketCanFrame& source, CanFrame* frame, std::string* error) {
  if (frame == nullptr) {
    if (error != nullptr) {
      *error = "CAN frame output is null";
    }
    return false;
  }
  if (source.error) {
    if (error != nullptr) {
      *error = "SocketCAN error frame mask=" + std::to_string(source.error_mask);
    }
    return false;
  }
  CanFrame result(source.id, source.data, source.length, source.extended, source.remote, source.fd,
                  source.brs, source.esi);
  if (!result.IsValid()) {
    if (error != nullptr) {
      *error = "invalid SocketCAN data frame";
    }
    return false;
  }
  *frame = result;
  return true;
}

}  // namespace can
}  // namespace cockpit
