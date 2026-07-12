#pragma once

#include <string>

#include "cockpit/navigator/common/module_api.h"

namespace cockpit {
namespace navigator {

class ModuleLoader {
 public:
  ModuleLoader() = default;
  ~ModuleLoader();

  ModuleLoader(const ModuleLoader&) = delete;
  ModuleLoader& operator=(const ModuleLoader&) = delete;

  bool Load(const std::string& path, const std::string& expected_name, std::string* error);
  int Start(const std::string& config_path) const;
  void Stop() const;
  void Unload();

 private:
  void* handle_{nullptr};
  const CockpitModuleApi* api_{nullptr};
};

}  // namespace navigator
}  // namespace cockpit
