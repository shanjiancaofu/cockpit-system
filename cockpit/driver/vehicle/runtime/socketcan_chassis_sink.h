#pragma once

#include <memory>
#include <string>

#include "cockpit/driver/platform/socketcan/socket_can.h"
#include "cockpit/driver/vehicle/chassis_command_sink.h"

namespace cockpit::vehicle {

class SocketCanChassisSink final : public ChassisCommandSink {
 public:
  static std::unique_ptr<SocketCanChassisSink> OpenVcanOnly(const std::string& interface_name,
                                                            std::string* error = nullptr);
  static std::unique_ptr<SocketCanChassisSink> OpenHardware(const std::string& interface_name,
                                                            std::string* error = nullptr);

  bool Send(const SafeChassisCommand& command, std::string* error) override;
  const std::string& interface_name() const {
    return interface_name_;
  }

 private:
  SocketCanChassisSink(std::string interface_name, can::SocketCan socket);

  std::string interface_name_;
  can::SocketCan socket_;
};

}  // namespace cockpit::vehicle
