#include "tools/cockpit-ctl/runtime_command.h"

#include <iostream>
#include <string>

#include "cockpit/navigator/connection/ipc_connector.h"
#include "tools/diagnostics/cli_output.h"

namespace cockpit {
namespace ctl {
namespace runtime_command {

int Run(int argc, char** argv, const runtime::Args& args) {
  if (argc < 3) {
    std::cerr << "runtime command requires status, mode, reload, switch, start, stop or restart\n";
    return diagnostics::ToInt(diagnostics::ExitCode::kInvalidArguments);
  }

  const std::string action = argv[2];
  std::string request;
  if (action == "status" || action == "mode" || action == "reload") {
    request = action;
  } else if (action == "switch" || action == "start" || action == "stop" || action == "restart") {
    if (argc < 4 || std::string(argv[3]).rfind("--", 0) == 0) {
      std::cerr << "runtime " << action << " requires a mode or module name\n";
      return diagnostics::ToInt(diagnostics::ExitCode::kInvalidArguments);
    }
    request = action + " " + argv[3];
  } else {
    std::cerr << "unknown runtime command: " << action << '\n';
    return diagnostics::ToInt(diagnostics::ExitCode::kInvalidArguments);
  }

  const std::string socket_path = args.GetString("socket", "/tmp/cockpit-navigator.sock");
  constexpr int kQueryTimeoutMs = 1000;
  constexpr int kMutationTimeoutMs = 15000;
  const int response_timeout_ms =
      action == "status" || action == "mode" ? kQueryTimeoutMs : kMutationTimeoutMs;
  std::string response;
  std::string error;
  if (!navigator::IpcConnector::SendRequest(socket_path, request, &response, &error,
                                            response_timeout_ms)) {
    std::cerr << error << '\n';
    return diagnostics::ToInt(diagnostics::ExitCode::kOperationFailed);
  }
  std::cout << response;
  return response.rfind("OK", 0) == 0 ? diagnostics::ToInt(diagnostics::ExitCode::kSuccess)
                                      : diagnostics::ToInt(diagnostics::ExitCode::kOperationFailed);
}

}  // namespace runtime_command
}  // namespace ctl
}  // namespace cockpit
