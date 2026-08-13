#pragma once

#include <string>

#include "cockpit/drivers/socketcan/socket_can.h"
#include "cockpit/modules/can/can_frame.h"

namespace cockpit {
namespace can {

SocketCanFrame ToSocketCanFrame(const CanFrame& frame);
bool FromSocketCanFrame(const SocketCanFrame& source, CanFrame* frame,
                        std::string* error = nullptr);

}  // namespace can
}  // namespace cockpit
