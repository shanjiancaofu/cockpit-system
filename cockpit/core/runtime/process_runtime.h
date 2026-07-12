#pragma once

#include <string>

#include "cockpit/core/config/system_config.h"
#include "cockpit/core/runtime/args.h"

namespace cockpit {
namespace runtime {

class ProcessRuntime {
 public:
  static ProcessRuntime Create(int argc, char** argv, const std::string& process_name);

  const Args& args() const {
    return args_;
  }
  const config::SystemConfig& config() const {
    return config_;
  }
  const std::string& config_path() const {
    return config_path_;
  }
  const std::string& process_name() const {
    return process_name_;
  }

  bool ShouldStop() const;
  void MarkStopped() const;

 private:
  ProcessRuntime(std::string process_name, std::string config_path, Args args,
                 config::SystemConfig config);

  std::string process_name_;
  std::string config_path_;
  Args args_;
  config::SystemConfig config_;
};

}  // namespace runtime
}  // namespace cockpit
