#pragma once

#include <string>

#include "cockpit/navigator/connection/ipc_connector.h"
#include "cockpit/navigator/process/process_manager.h"
#include "cockpit/navigator/run_config/run_config.h"

namespace cockpit {
namespace navigator {

class Navigator {
 public:
  Navigator(RunConfig config, std::string executable_path, std::string module_dir,
            std::string module_config_path);

  int Run();

 private:
  std::string ExecuteCommand(const std::string& command);
  std::string StatusText() const;

  RunConfig config_;
  ProcessManager process_manager_;
  IpcConnector ipc_;
  bool stop_requested_{false};
};

int RunModuleChild(const std::string& module_name, const std::string& library_path,
                   const std::string& config_path, int ready_fd);

}  // namespace navigator
}  // namespace cockpit
