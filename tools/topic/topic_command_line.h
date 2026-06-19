#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace cockpit {
namespace topic {

struct CommandLine {
  std::string command;
  std::vector<std::string> positionals;
  std::map<std::string, std::string> options;
  std::map<std::string, bool> flags;
};

CommandLine ParseCommandLine(int argc, char** argv);
std::string Option(const CommandLine& line, const std::string& name,
                   const std::string& default_value);
int OptionInt(const CommandLine& line, const std::string& name, int default_value);
bool HasFlag(const CommandLine& line, const std::string& name);
std::string Join(const std::vector<std::string>& values, std::size_t begin);

}  // namespace topic
}  // namespace cockpit
