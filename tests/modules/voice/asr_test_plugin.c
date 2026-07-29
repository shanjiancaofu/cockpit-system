#include <stdio.h>
#include <string.h>

#include "cockpit/modules/voice/asr/asr_plugin_api.h"

static int plugin_instance = 1;

static void* Create(const char* config_path, char* error, uint64_t error_capacity) {
  if (strcmp(config_path, "/etc/cockpit/test-asr.yaml") != 0) {
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

static int32_t Recognize(void* instance, const CockpitAsrInput* input, CockpitAsrOutput* output) {
  if (instance != &plugin_instance || input == NULL || output == NULL ||
      input->struct_size < sizeof(CockpitAsrInput) ||
      output->struct_size < sizeof(CockpitAsrOutput) || input->samples == NULL ||
      input->sample_rate_hz != 16000U || input->channels != 1U || output->text == NULL ||
      output->text_capacity == 0) {
    return COCKPIT_ASR_STATUS_INVALID_ARGUMENT;
  }
  snprintf(output->text, (size_t)output->text_capacity, "plugin samples=%llu flags=%u",
           (unsigned long long)input->sample_count, input->flags);
  output->confidence = 0.75F;
  return COCKPIT_ASR_STATUS_OK;
}

static const CockpitAsrPluginApi plugin_api = {
    COCKPIT_ASR_PLUGIN_ABI_VERSION,
    sizeof(CockpitAsrPluginApi),
    "test-plugin",
    Create,
    Destroy,
    Recognize,
};

const CockpitAsrPluginApi* CockpitAsrPluginGetApi(void) {
  return &plugin_api;
}
