#include "core/runtime/Args.h"

#include <stdexcept>

namespace cockpit {
namespace runtime {

Args Args::Parse(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    std::string key = argv[i];
    if (key.rfind("--", 0) != 0) {
      continue;
    }

    key = key.substr(2);
    if (i + 1 < argc) {
      const std::string next = argv[i + 1];
      if (next.rfind("--", 0) != 0) {
        args.values_[key] = next;
        ++i;
        continue;
      }
    }
    args.flags_[key] = true;
  }
  return args;
}

bool Args::HasFlag(const std::string& name) const {
  return flags_.find(name) != flags_.end();
}

std::string Args::GetString(const std::string& name, const std::string& default_value) const {
  const auto it = values_.find(name);
  if (it == values_.end()) {
    return default_value;
  }
  return it->second;
}

int Args::GetInt(const std::string& name, int default_value) const {
  const auto it = values_.find(name);
  if (it == values_.end()) {
    return default_value;
  }
  try {
    return std::stoi(it->second);
  } catch (const std::exception&) {
    return default_value;
  }
}

}  // namespace runtime
}  // namespace cockpit
