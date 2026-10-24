#pragma once

#include <string>

#include "cockpit/driver/can/can_frame.h"
#include "cockpit/driver/platform/socketcan/socket_can.h"

namespace cockpit {
namespace can {

SocketCanFrame ToSocketCanFrame(const CanFrame& frame);
bool FromSocketCanFrame(const SocketCanFrame& source, CanFrame* frame,
                        std::string* error = nullptr);

}  // namespace can
}  // namespace cockpit
