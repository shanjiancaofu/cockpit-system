#pragma once

#include <cstddef>
#include <cstdint>

#define COCKPIT_MODULE_ABI_VERSION 1U
#define COCKPIT_MODULE_API_SYMBOL "CockpitModuleGetApi"

extern "C" {

struct CockpitModuleApi {
  std::uint32_t abi_version;
  std::size_t struct_size;
  const char* name;
  int (*start)(const char* config_path);
  void (*stop)();
};

using CockpitModuleGetApiFn = const CockpitModuleApi* (*)();
}
