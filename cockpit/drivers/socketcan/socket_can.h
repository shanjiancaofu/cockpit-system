#pragma once

#include <string>

#include "cockpit/modules/can/can_frame.h"

namespace cockpit {
namespace can {

enum class CanIoStatus {
  kOk,
  kTimeout,
  kClosed,
  kError,
};

class SocketCan {
 public:
  SocketCan() = default;
  ~SocketCan();

  SocketCan(const SocketCan&) = delete;
  SocketCan& operator=(const SocketCan&) = delete;

  SocketCan(SocketCan&& other) noexcept;
  SocketCan& operator=(SocketCan&& other) noexcept;

  bool Open(const std::string& interface_name, std::string* error);
  void Close();
  bool IsOpen() const {
    return fd_ >= 0;
  }

  bool Send(const CanFrame& frame, std::string* error) const;
  CanIoStatus Receive(CanFrame* frame, int timeout_ms, std::string* error) const;

 private:
  static void SetError(std::string* error, const std::string& message);

  int fd_ = -1;
};

}  // namespace can
}  // namespace cockpit
