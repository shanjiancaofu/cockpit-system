#include "cockpit/modules/audio/vad/vad_plugin_api.h"

static const CockpitVadPluginApi plugin_api = {
    COCKPIT_VAD_PLUGIN_ABI_VERSION + 1U,
    sizeof(CockpitVadPluginApi),
    "invalid-vad-plugin",
    0,
    0,
    0,
    0,
};

const CockpitVadPluginApi* CockpitVadPluginGetApi(void) {
  return &plugin_api;
}
