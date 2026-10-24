#pragma once

#include <map>
#include <string>

namespace cockpit {
namespace runtime {

class Args {
 public:
  static Args Parse(int argc, char** argv);

  bool HasFlag(const std::string& name) const;
  std::string GetString(const std::string& name, const std::string& default_value) const;
  int GetInt(const std::string& name, int default_value) const;

 private:
  std::map<std::string, std::string> values_;
  std::map<std::string, bool> flags_;
};

}  // namespace runtime
}  // namespace cockpit
