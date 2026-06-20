#pragma once

#include <map>
#include <string>

namespace cockpit {
namespace config {

class Config {
 public:
  static Config LoadFromFile(const std::string& path);

  std::string GetString(const std::string& key, const std::string& default_value) const;
  int GetInt(const std::string& key, int default_value) const;
  bool GetBool(const std::string& key, bool default_value) const;

 private:
  std::map<std::string, std::string> values_;
};

}  // namespace config
}  // namespace cockpit
