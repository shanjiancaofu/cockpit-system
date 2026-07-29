#pragma once

#include <stddef.h>
#include <stdint.h>

#define COCKPIT_VAD_PLUGIN_ABI_VERSION 1U
#define COCKPIT_VAD_PLUGIN_API_SYMBOL "CockpitVadPluginGetApi"

#define COCKPIT_VAD_STATUS_OK 0
#define COCKPIT_VAD_STATUS_INVALID_ARGUMENT 1
#define COCKPIT_VAD_STATUS_RUNTIME_ERROR 2

#define COCKPIT_VAD_INPUT_DISCONTINUOUS (1U << 0U)
#define COCKPIT_VAD_INPUT_DROPPED_BEFORE (1U << 1U)

#define COCKPIT_VAD_STATE_SILENCE 0U
#define COCKPIT_VAD_STATE_SPEECH 1U

#if defined(__GNUC__) || defined(__clang__)
#define COCKPIT_VAD_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define COCKPIT_VAD_PLUGIN_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The host owns the sample buffer. A plugin may buffer samples internally,
 * but it must copy anything needed after analyze() returns.
 */
typedef struct CockpitVadInput {
  uint32_t struct_size;
  uint32_t flags;
  uint32_t sample_rate_hz;
  uint32_t channels;
  uint64_t sample_count;
  const int16_t* samples;
  int64_t capture_time_ns;
} CockpitVadInput;

/* speech_probability must be finite and in the inclusive range [0, 1]. */
typedef struct CockpitVadOutput {
  uint32_t struct_size;
  uint32_t state;
  float speech_probability;
  uint32_t reserved;
  char* error;
  uint64_t error_capacity;
} CockpitVadOutput;

typedef struct CockpitVadPluginApi {
  uint32_t abi_version;
  uint32_t struct_size;
  const char* name;
  void* (*create)(const char* config_path, char* error, uint64_t error_capacity);
  void (*destroy)(void* instance);
  void (*reset)(void* instance);
  int32_t (*analyze)(void* instance, const CockpitVadInput* input, CockpitVadOutput* output);
} CockpitVadPluginApi;

typedef const CockpitVadPluginApi* (*CockpitVadPluginGetApiFn)(void);

/*
 * The returned table must remain valid until process exit. New fields may
 * only be appended in a future ABI.
 */
COCKPIT_VAD_PLUGIN_EXPORT const CockpitVadPluginApi* CockpitVadPluginGetApi(void);

#ifdef __cplusplus
}
#endif
