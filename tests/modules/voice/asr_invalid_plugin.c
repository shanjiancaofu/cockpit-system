#include "cockpit/modules/voice/asr/asr_plugin_api.h"

static const CockpitAsrPluginApi plugin_api = {
    COCKPIT_ASR_PLUGIN_ABI_VERSION + 1U, sizeof(CockpitAsrPluginApi), "invalid-plugin", 0, 0, 0,
};

const CockpitAsrPluginApi* CockpitAsrPluginGetApi(void) {
  return &plugin_api;
}
