#include "cockpit/navigator/dl_api/module_loader.h"

#include <dlfcn.h>

#include <string>

namespace cockpit {
namespace navigator {

ModuleLoader::~ModuleLoader() {
  Unload();
}

bool ModuleLoader::Load(const std::string& path, const std::string& expected_name,
                        std::string* error) {
  Unload();
  handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (handle_ == nullptr) {
    *error = "failed to load " + path + ": " + dlerror();
    return false;
  }

  dlerror();
  auto get_api = reinterpret_cast<CockpitModuleGetApiFn>(dlsym(handle_, COCKPIT_MODULE_API_SYMBOL));
  const char* symbol_error = dlerror();
  if (symbol_error != nullptr) {
    *error = "missing " COCKPIT_MODULE_API_SYMBOL " in " + path + ": " + symbol_error;
    Unload();
    return false;
  }

  api_ = get_api();
  if (api_ == nullptr || api_->struct_size < sizeof(CockpitModuleApi) ||
      api_->abi_version != COCKPIT_MODULE_ABI_VERSION || api_->name == nullptr ||
      api_->start == nullptr || api_->stop == nullptr) {
    *error = "invalid module API in " + path;
    Unload();
    return false;
  }
  if (expected_name != api_->name) {
    *error = "module name mismatch: expected " + expected_name + ", got " + api_->name;
    Unload();
    return false;
  }
  return true;
}

int ModuleLoader::Start(const std::string& config_path) const {
  return api_->start(config_path.c_str());
}

void ModuleLoader::Stop() const {
  api_->stop();
}

void ModuleLoader::Unload() {
  api_ = nullptr;
  if (handle_ != nullptr) {
    dlclose(handle_);
    handle_ = nullptr;
  }
}

}  // namespace navigator
}  // namespace cockpit
