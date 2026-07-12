#pragma once

#include <string>

namespace cockpit {
namespace navigator {

class IpcConnector {
 public:
  IpcConnector() = default;
  ~IpcConnector();

  IpcConnector(const IpcConnector&) = delete;
  IpcConnector& operator=(const IpcConnector&) = delete;

  bool Open(const std::string& socket_path, std::string* error);
  int WaitForRequest(int timeout_ms, std::string* request);
  void ReplyAndClose(int client_fd, const std::string& response) const;
  void Close();

  static bool SendRequest(const std::string& socket_path, const std::string& request,
                          std::string* response, std::string* error);

 private:
  int socket_fd_{-1};
  std::string socket_path_;
};

}  // namespace navigator
}  // namespace cockpit
