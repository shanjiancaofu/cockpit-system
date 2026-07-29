#pragma once

#include <memory>
#include <string>

#include "cockpit/modules/voice/asr/asr_plugin_api.h"
#include "cockpit/modules/voice/asr/speech_recognizer.h"

namespace cockpit {
namespace voice {

class PluginSpeechRecognizer final : public SpeechRecognizer {
 public:
  static std::unique_ptr<PluginSpeechRecognizer> Load(const std::string& library_path,
                                                      const std::string& config_path,
                                                      std::string* error);

  ~PluginSpeechRecognizer() override;

  PluginSpeechRecognizer(const PluginSpeechRecognizer&) = delete;
  PluginSpeechRecognizer& operator=(const PluginSpeechRecognizer&) = delete;

  SpeechRecognitionResult Recognize(const audio::SpeechSegment& segment) override;

 private:
  PluginSpeechRecognizer(void* handle, const CockpitAsrPluginApi* api, void* instance);

  void* handle_{nullptr};
  const CockpitAsrPluginApi* api_{nullptr};
  void* instance_{nullptr};
};

}  // namespace voice
}  // namespace cockpit
