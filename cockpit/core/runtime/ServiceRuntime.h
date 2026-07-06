#pragma once

#include <string>

#include "cockpit/core/config/system_config.h"
#include "cockpit/core/runtime/Args.h"

namespace cockpit {
namespace runtime {

class ServiceRuntime {
 public:
  static ServiceRuntime Create(int argc, char** argv, const std::string& service_name);

  const Args& args() const {
    return args_;
  }
  const config::SystemConfig& config() const {
    return config_;
  }
  const std::string& config_path() const {
    return config_path_;
  }
  const std::string& service_name() const {
    return service_name_;
  }

  bool ShouldStop() const;
  void MarkStopped() const;

 private:
  ServiceRuntime(std::string service_name, std::string config_path, Args args,
                 config::SystemConfig config);

  std::string service_name_;
  std::string config_path_;
  Args args_;
  config::SystemConfig config_;
};

}  // namespace runtime
}  // namespace cockpit
