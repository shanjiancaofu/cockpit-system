#include "cockpit/modules/audio/vad/plugin_voice_activity_detector.h"

#include <dlfcn.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace cockpit {
namespace audio {
namespace {

constexpr std::size_t kErrorCapacity = 1024U;

std::string LoaderError(const std::string& prefix) {
  const char* error = dlerror();
  return error == nullptr ? prefix : prefix + ": " + error;
}

}  // namespace

std::unique_ptr<PluginVoiceActivityDetector> PluginVoiceActivityDetector::Load(
    const std::string& library_path, const std::string& config_path, std::string* error) {
  if (error == nullptr) {
    return nullptr;
  }
  error->clear();
  const std::filesystem::path path(library_path);
  if (!path.is_absolute() || path.extension() != ".so") {
    *error = "VAD plugin must use an absolute .so path";
    return nullptr;
  }

  void* handle = dlopen(library_path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (handle == nullptr) {
    *error = LoaderError("failed to load VAD plugin " + library_path);
    return nullptr;
  }

  dlerror();
  auto get_api =
      reinterpret_cast<CockpitVadPluginGetApiFn>(dlsym(handle, COCKPIT_VAD_PLUGIN_API_SYMBOL));
  const char* symbol_error = dlerror();
  if (symbol_error != nullptr) {
    *error = "missing " COCKPIT_VAD_PLUGIN_API_SYMBOL " in " + library_path + ": " + symbol_error;
    dlclose(handle);
    return nullptr;
  }

  const CockpitVadPluginApi* api = get_api();
  if (api == nullptr || api->abi_version != COCKPIT_VAD_PLUGIN_ABI_VERSION ||
      api->struct_size < sizeof(CockpitVadPluginApi) || api->name == nullptr ||
      api->name[0] == '\0' || api->create == nullptr || api->destroy == nullptr ||
      api->reset == nullptr || api->analyze == nullptr) {
    *error = "invalid VAD plugin API in " + library_path;
    dlclose(handle);
    return nullptr;
  }

  std::array<char, kErrorCapacity> plugin_error{};
  void* instance = api->create(config_path.c_str(), plugin_error.data(), plugin_error.size());
  if (instance == nullptr) {
    *error = plugin_error[0] == '\0' ? "VAD plugin initialization failed" : plugin_error.data();
    dlclose(handle);
    return nullptr;
  }

  return std::unique_ptr<PluginVoiceActivityDetector>(
      new PluginVoiceActivityDetector(handle, api, instance));
}

PluginVoiceActivityDetector::PluginVoiceActivityDetector(void* handle,
                                                         const CockpitVadPluginApi* api,
                                                         void* instance)
    : handle_(handle), api_(api), instance_(instance) {
}

PluginVoiceActivityDetector::~PluginVoiceActivityDetector() {
  api_->destroy(instance_);
  instance_ = nullptr;
  api_ = nullptr;
  // Keep a successfully initialized plugin mapped until process exit. Its
  // private runtime may retain TLS, worker threads, or static state.
  handle_ = nullptr;
}

VoiceActivityResult PluginVoiceActivityDetector::Analyze(const AudioFrame& frame) {
  std::uint32_t flags = 0;
  if (frame.HasFlag(AudioFrameFlag::kDiscontinuity)) {
    flags |= COCKPIT_VAD_INPUT_DISCONTINUOUS;
  }
  if (frame.HasFlag(AudioFrameFlag::kDroppedBefore)) {
    flags |= COCKPIT_VAD_INPUT_DROPPED_BEFORE;
  }
  const CockpitVadInput input{
      sizeof(CockpitVadInput),   flags,
      AudioFrame::kSampleRateHz, AudioFrame::kChannels,
      frame.samples().size(),    frame.samples().data(),
      frame.capture_time_ns(),
  };
  std::array<char, kErrorCapacity> plugin_error{};
  CockpitVadOutput output{
      sizeof(CockpitVadOutput), COCKPIT_VAD_STATE_SILENCE, 0.0F, 0,
      plugin_error.data(),      plugin_error.size(),
  };

  const std::int32_t result = api_->analyze(instance_, &input, &output);
  plugin_error.back() = '\0';
  if (result != COCKPIT_VAD_STATUS_OK) {
    throw std::runtime_error(plugin_error[0] == '\0' ? "VAD plugin analysis failed"
                                                     : plugin_error.data());
  }
  if (output.state != COCKPIT_VAD_STATE_SILENCE && output.state != COCKPIT_VAD_STATE_SPEECH) {
    throw std::runtime_error("VAD plugin returned an invalid state");
  }
  if (!std::isfinite(output.speech_probability) || output.speech_probability < 0.0F ||
      output.speech_probability > 1.0F) {
    throw std::runtime_error("VAD plugin returned an invalid speech probability");
  }

  const VoiceActivityState next_state = output.state == COCKPIT_VAD_STATE_SPEECH
                                            ? VoiceActivityState::kSpeech
                                            : VoiceActivityState::kSilence;
  const bool state_changed = state_ != next_state;
  state_ = next_state;
  return {state_, output.speech_probability, state_changed};
}

void PluginVoiceActivityDetector::Reset() {
  api_->reset(instance_);
  state_ = VoiceActivityState::kSilence;
}

}  // namespace audio
}  // namespace cockpit
