#pragma once

#include <memory>
#include <string>

#include "cockpit/modules/audio/vad/vad_plugin_api.h"
#include "cockpit/modules/audio/vad/voice_activity_detector.h"

namespace cockpit {
namespace audio {

class PluginVoiceActivityDetector final : public VoiceActivityDetector {
 public:
  static std::unique_ptr<PluginVoiceActivityDetector> Load(const std::string& library_path,
                                                           const std::string& config_path,
                                                           std::string* error);

  ~PluginVoiceActivityDetector() override;

  PluginVoiceActivityDetector(const PluginVoiceActivityDetector&) = delete;
  PluginVoiceActivityDetector& operator=(const PluginVoiceActivityDetector&) = delete;

  VoiceActivityResult Analyze(const AudioFrame& frame) override;
  void Reset() override;

 private:
  PluginVoiceActivityDetector(void* handle, const CockpitVadPluginApi* api, void* instance);

  void* handle_{nullptr};
  const CockpitVadPluginApi* api_{nullptr};
  void* instance_{nullptr};
  VoiceActivityState state_{VoiceActivityState::kSilence};
};

}  // namespace audio
}  // namespace cockpit
