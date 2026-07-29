#pragma once

#include <stddef.h>
#include <stdint.h>

#define COCKPIT_ASR_PLUGIN_ABI_VERSION 1U
#define COCKPIT_ASR_PLUGIN_API_SYMBOL "CockpitAsrPluginGetApi"

#define COCKPIT_ASR_STATUS_OK 0
#define COCKPIT_ASR_STATUS_INVALID_ARGUMENT 1
#define COCKPIT_ASR_STATUS_RUNTIME_ERROR 2

#define COCKPIT_ASR_INPUT_TRUNCATED (1U << 0U)
#define COCKPIT_ASR_INPUT_DISCONTINUOUS (1U << 1U)

#if defined(__GNUC__) || defined(__clang__)
#define COCKPIT_ASR_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define COCKPIT_ASR_PLUGIN_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* All pointers remain owned by the caller and are valid only for the call. */
typedef struct CockpitAsrInput {
  uint32_t struct_size;
  uint32_t flags;
  uint32_t sample_rate_hz;
  uint32_t channels;
  uint64_t sample_count;
  const int16_t* samples;
  int64_t start_time_ns;
  int64_t end_time_ns;
} CockpitAsrInput;

/* The host owns both buffers. The plugin must write NUL-terminated strings. */
typedef struct CockpitAsrOutput {
  uint32_t struct_size;
  uint32_t reserved;
  char* text;
  uint64_t text_capacity;
  float confidence;
  char* error;
  uint64_t error_capacity;
} CockpitAsrOutput;

typedef struct CockpitAsrPluginApi {
  uint32_t abi_version;
  uint32_t struct_size;
  const char* name;
  void* (*create)(const char* config_path, char* error, uint64_t error_capacity);
  void (*destroy)(void* instance);
  int32_t (*recognize)(void* instance, const CockpitAsrInput* input, CockpitAsrOutput* output);
} CockpitAsrPluginApi;

typedef const CockpitAsrPluginApi* (*CockpitAsrPluginGetApiFn)(void);

/*
 * A plugin exports exactly this entry point. The returned table must remain
 * valid until process exit; new fields may only be appended in a future ABI.
 */
COCKPIT_ASR_PLUGIN_EXPORT const CockpitAsrPluginApi* CockpitAsrPluginGetApi(void);

#ifdef __cplusplus
}
#endif
