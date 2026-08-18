#pragma once

#include <cstddef>
#include <cstdint>

#define COCKPIT_MODULE_ABI_VERSION 2U
#define COCKPIT_MODULE_API_SYMBOL "CockpitModuleGetApi"

#if defined(__GNUC__) || defined(__clang__)
#define COCKPIT_MODULE_EXPORT __attribute__((visibility("default")))
#else
#define COCKPIT_MODULE_EXPORT
#endif

extern "C" {

struct CockpitModuleApi {
  std::uint32_t abi_version;
  std::size_t struct_size;
  const char* name;
  int (*start)(const char* config_path);
  void (*stop)();
  int (*poll)();
};

using CockpitModuleGetApiFn = const CockpitModuleApi* (*)();
COCKPIT_MODULE_EXPORT const CockpitModuleApi* CockpitModuleGetApi();
}
