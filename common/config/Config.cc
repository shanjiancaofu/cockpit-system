#include "common/config/Config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace cockpit {
namespace config {
namespace {

std::string Trim(const std::string& value) {
  const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  });
  const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  }).base();
  if (begin >= end) {
    return "";
  }
  return std::string(begin, end);
}

std::string StripQuotes(const std::string& value) {
  if (value.size() >= 2 &&
      ((value.front() == '"' && value.back() == '"') ||
       (value.front() == '\'' && value.back() == '\''))) {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

}  // namespace

Config Config::LoadFromFile(const std::string& path) {
  std::ifstream input(path);
  if (!input.is_open()) {
    throw std::runtime_error("failed to open config file: " + path);
  }

  Config config;
  std::string section;
  std::string line;
  while (std::getline(input, line)) {
    const auto comment = line.find('#');
    if (comment != std::string::npos) {
      line = line.substr(0, comment);
    }
    line = Trim(line);
    if (line.empty()) {
      continue;
    }

    const auto colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }

    std::string key = Trim(line.substr(0, colon));
    std::string value = Trim(line.substr(colon + 1));
    if (value.empty()) {
      section = key;
      continue;
    }

    value = StripQuotes(value);
    const std::string full_key = section.empty() ? key : section + "." + key;
    config.values_[full_key] = value;
  }
  return config;
}

std::string Config::GetString(const std::string& key, const std::string& default_value) const {
  const auto it = values_.find(key);
  if (it == values_.end()) {
    return default_value;
  }
  return it->second;
}

int Config::GetInt(const std::string& key, int default_value) const {
  const auto it = values_.find(key);
  if (it == values_.end()) {
    return default_value;
  }
  try {
    return std::stoi(it->second);
  } catch (...) {
    return default_value;
  }
}

bool Config::GetBool(const std::string& key, bool default_value) const {
  const auto it = values_.find(key);
  if (it == values_.end()) {
    return default_value;
  }
  std::string value = it->second;
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  if (value == "true" || value == "1" || value == "yes" || value == "on") {
    return true;
  }
  if (value == "false" || value == "0" || value == "no" || value == "off") {
    return false;
  }
  return default_value;
}

}  // namespace config
}  // namespace cockpit
