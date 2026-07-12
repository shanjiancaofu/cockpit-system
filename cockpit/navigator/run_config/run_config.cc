#include "cockpit/navigator/run_config/run_config.h"

#include <yaml-cpp/yaml.h>

#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cockpit {
namespace navigator {
namespace {

template <typename T>
T ReadScalar(const YAML::Node& parent, const char* key, const T& default_value,
             const std::string& path) {
  const YAML::Node value = parent[key];
  if (!value) {
    return default_value;
  }
  if (!value.IsScalar()) {
    throw std::runtime_error(path + " must be a scalar");
  }
  try {
    return value.as<T>();
  } catch (const YAML::Exception& error) {
    throw std::runtime_error(path + " has invalid value: " + error.msg);
  }
}

bool IsValidName(const std::string& name) {
  if (name.empty()) {
    return false;
  }
  for (char value : name) {
    if (!std::islower(static_cast<unsigned char>(value)) &&
        !std::isdigit(static_cast<unsigned char>(value)) && value != '_' && value != '-') {
      return false;
    }
  }
  return true;
}

std::vector<std::string> ReadModuleList(const YAML::Node& value, const std::string& path) {
  if (!value.IsSequence()) {
    throw std::runtime_error(path + " must be a list");
  }
  std::vector<std::string> result;
  std::unordered_set<std::string> seen;
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (!value[i].IsScalar()) {
      throw std::runtime_error(path + " items must be scalars");
    }
    const std::string name = value[i].as<std::string>();
    if (!seen.insert(name).second) {
      throw std::runtime_error(path + " contains duplicate module " + name);
    }
    result.push_back(name);
  }
  return result;
}

}  // namespace

RunConfig RunConfig::LoadFromFile(const std::string& path) {
  YAML::Node root;
  try {
    root = YAML::LoadFile(path);
  } catch (const YAML::Exception& error) {
    throw std::runtime_error("failed to load navigator config " + path + ": " + error.msg);
  }
  if (!root.IsMap()) {
    throw std::runtime_error("navigator config root must be a map");
  }

  RunConfig config;
  config.initial_mode = ReadScalar(root, "initial_mode", config.initial_mode, "initial_mode");
  config.socket_path = ReadScalar(root, "socket_path", config.socket_path, "socket_path");
  config.startup_timeout_ms =
      ReadScalar(root, "startup_timeout_ms", config.startup_timeout_ms, "startup_timeout_ms");
  config.stop_timeout_ms =
      ReadScalar(root, "stop_timeout_ms", config.stop_timeout_ms, "stop_timeout_ms");

  const YAML::Node restart = root["restart"];
  if (restart && !restart.IsMap()) {
    throw std::runtime_error("restart must be a map");
  }
  if (restart) {
    config.max_restarts =
        ReadScalar(restart, "max_attempts", config.max_restarts, "restart.max_attempts");
    config.restart_window_seconds = ReadScalar(
        restart, "window_seconds", config.restart_window_seconds, "restart.window_seconds");
  }

  const YAML::Node modules = root["modules"];
  if (!modules || !modules.IsSequence()) {
    throw std::runtime_error("modules must be a list");
  }
  std::unordered_set<std::string> module_names;
  for (std::size_t i = 0; i < modules.size(); ++i) {
    if (!modules[i].IsMap()) {
      throw std::runtime_error("modules items must be maps");
    }
    ModuleConfig module;
    const std::string item_path = "modules[" + std::to_string(i) + "]";
    module.name = ReadScalar(modules[i], "name", module.name, item_path + ".name");
    module.library = ReadScalar(modules[i], "library", module.library, item_path + ".library");
    module.config_path =
        ReadScalar(modules[i], "config", module.config_path, item_path + ".config");
    if (!IsValidName(module.name)) {
      throw std::runtime_error(item_path + ".name must use lowercase letters, digits, _ or -");
    }
    if (module.library.empty()) {
      throw std::runtime_error(item_path + ".library must not be empty");
    }
    if (!module_names.insert(module.name).second) {
      throw std::runtime_error("duplicate module " + module.name);
    }
    if (!module.config_path.empty() && !std::filesystem::path(module.config_path).is_absolute()) {
      module.config_path = (std::filesystem::path(path).parent_path() / module.config_path)
                               .lexically_normal()
                               .string();
    }
    config.modules.push_back(std::move(module));
  }

  const YAML::Node modes = root["modes"];
  if (!modes || !modes.IsMap()) {
    throw std::runtime_error("modes must be a map");
  }
  for (const auto& item : modes) {
    const std::string mode_name = item.first.as<std::string>();
    if (!IsValidName(mode_name)) {
      throw std::runtime_error("mode name must use lowercase letters, digits, _ or -");
    }
    config.modes.emplace(mode_name, ReadModuleList(item.second, "modes." + mode_name));
  }

  if (config.initial_mode.empty() || config.modes.find(config.initial_mode) == config.modes.end()) {
    throw std::runtime_error("initial_mode must name a configured mode");
  }
  if (config.socket_path.empty()) {
    throw std::runtime_error("socket_path must not be empty");
  }
  if (config.startup_timeout_ms <= 0 || config.stop_timeout_ms <= 0 || config.max_restarts < 0 ||
      config.restart_window_seconds <= 0) {
    throw std::runtime_error("navigator timeouts must be positive and max_attempts non-negative");
  }
  for (const auto& mode : config.modes) {
    for (const std::string& module_name : mode.second) {
      if (module_names.find(module_name) == module_names.end()) {
        throw std::runtime_error("mode " + mode.first + " references unknown module " +
                                 module_name);
      }
    }
  }
  return config;
}

const ModuleConfig* RunConfig::FindModule(const std::string& name) const {
  for (const ModuleConfig& module : modules) {
    if (module.name == name) {
      return &module;
    }
  }
  return nullptr;
}

}  // namespace navigator
}  // namespace cockpit
