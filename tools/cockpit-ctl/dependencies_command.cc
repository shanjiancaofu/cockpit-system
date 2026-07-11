#include "dependencies_command.h"

#include <iostream>

namespace cockpit {
namespace ctl {
namespace dependencies {

int Run(const config::SystemConfig& config) {
  std::cout << "cockpit-system dependencies\n";
  for (const auto& dependency : config.runtime().dependencies) {
    std::cout << dependency.service << "\n  required: [";
    for (std::size_t index = 0; index < dependency.required.size(); ++index) {
      if (index > 0) {
        std::cout << ", ";
      }
      std::cout << dependency.required[index];
    }
    std::cout << "]\n  optional: [";
    for (std::size_t index = 0; index < dependency.optional.size(); ++index) {
      if (index > 0) {
        std::cout << ", ";
      }
      std::cout << dependency.optional[index];
    }
    std::cout << "]\n";
  }
  return 0;
}

}  // namespace dependencies
}  // namespace ctl
}  // namespace cockpit
