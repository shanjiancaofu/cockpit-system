#include "topic_command_line.h"

#include <sstream>

namespace cockpit {
namespace topic {

CommandLine ParseCommandLine(int argc, char** argv) {
  CommandLine line;
  if (argc >= 2) {
    line.command = argv[1];
  }

  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg.rfind("--", 0) == 0) {
      const std::string name = arg.substr(2);
      if (i + 1 < argc) {
        const std::string next = argv[i + 1];
        if (next.rfind("--", 0) != 0) {
          line.options[name] = next;
          ++i;
          continue;
        }
      }
      line.flags[name] = true;
    } else {
      line.positionals.push_back(arg);
    }
  }
  return line;
}

std::string Option(const CommandLine& line, const std::string& name,
                   const std::string& default_value) {
  const auto it = line.options.find(name);
  return it == line.options.end() ? default_value : it->second;
}

int OptionInt(const CommandLine& line, const std::string& name, int default_value) {
  const auto it = line.options.find(name);
  if (it == line.options.end()) {
    return default_value;
  }
  try {
    return std::stoi(it->second);
  } catch (...) {
    return default_value;
  }
}

bool HasFlag(const CommandLine& line, const std::string& name) {
  return line.flags.find(name) != line.flags.end();
}

std::string Join(const std::vector<std::string>& values, std::size_t begin) {
  std::ostringstream out;
  for (std::size_t i = begin; i < values.size(); ++i) {
    if (i != begin) {
      out << ' ';
    }
    out << values[i];
  }
  return out.str();
}

}  // namespace topic
}  // namespace cockpit
