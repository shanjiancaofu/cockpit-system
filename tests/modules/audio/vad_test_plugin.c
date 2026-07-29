#include <stdio.h>
#include <string.h>

#include "cockpit/modules/audio/vad/vad_plugin_api.h"

static int plugin_instance = 1;

static void* Create(const char* config_path, char* error, uint64_t error_capacity) {
  if (strcmp(config_path, "/etc/cockpit/test-vad.yaml") != 0) {
    if (error != NULL && error_capacity > 0) {
      snprintf(error, (size_t)error_capacity, "unexpected plugin config path");
    }
    return NULL;
  }
  return &plugin_instance;
}

static void Destroy(void* instance) {
  (void)instance;
}

static void Reset(void* instance) {
  (void)instance;
}

static int32_t Analyze(void* instance, const CockpitVadInput* input, CockpitVadOutput* output) {
  if (instance != &plugin_instance || input == NULL || output == NULL ||
      input->struct_size < sizeof(CockpitVadInput) ||
      output->struct_size < sizeof(CockpitVadOutput) || input->samples == NULL ||
      input->sample_rate_hz != 16000U || input->channels != 1U || input->sample_count != 320U ||
      (input->flags & COCKPIT_VAD_INPUT_DISCONTINUOUS) == 0U) {
    return COCKPIT_VAD_STATUS_INVALID_ARGUMENT;
  }
  output->state = input->samples[0] == 0 ? COCKPIT_VAD_STATE_SILENCE : COCKPIT_VAD_STATE_SPEECH;
  output->speech_probability = output->state == COCKPIT_VAD_STATE_SPEECH ? 0.8F : 0.1F;
  return COCKPIT_VAD_STATUS_OK;
}

static const CockpitVadPluginApi plugin_api = {
    COCKPIT_VAD_PLUGIN_ABI_VERSION,
    sizeof(CockpitVadPluginApi),
    "test-vad-plugin",
    Create,
    Destroy,
    Reset,
    Analyze,
};

const CockpitVadPluginApi* CockpitVadPluginGetApi(void) {
  return &plugin_api;
}
