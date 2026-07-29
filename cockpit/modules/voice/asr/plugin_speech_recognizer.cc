#include "cockpit/modules/voice/asr/plugin_speech_recognizer.h"

#include <dlfcn.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

#include "cockpit/modules/audio/frames/audio_frame.h"

namespace cockpit {
namespace voice {
namespace {

constexpr std::size_t kTranscriptCapacity = 16U * 1024U;
constexpr std::size_t kErrorCapacity = 1024U;

std::string LoaderError(const std::string& prefix) {
  const char* error = dlerror();
  return error == nullptr ? prefix : prefix + ": " + error;
}

}  // namespace

std::unique_ptr<PluginSpeechRecognizer> PluginSpeechRecognizer::Load(
    const std::string& library_path, const std::string& config_path, std::string* error) {
  if (error == nullptr) {
    return nullptr;
  }
  error->clear();
  const std::filesystem::path path(library_path);
  if (!path.is_absolute() || path.extension() != ".so") {
    *error = "ASR plugin must use an absolute .so path";
    return nullptr;
  }

  void* handle = dlopen(library_path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (handle == nullptr) {
    *error = LoaderError("failed to load ASR plugin " + library_path);
    return nullptr;
  }

  dlerror();
  auto get_api =
      reinterpret_cast<CockpitAsrPluginGetApiFn>(dlsym(handle, COCKPIT_ASR_PLUGIN_API_SYMBOL));
  const char* symbol_error = dlerror();
  if (symbol_error != nullptr) {
    *error = "missing " COCKPIT_ASR_PLUGIN_API_SYMBOL " in " + library_path + ": " + symbol_error;
    dlclose(handle);
    return nullptr;
  }

  const CockpitAsrPluginApi* api = get_api();
  if (api == nullptr || api->abi_version != COCKPIT_ASR_PLUGIN_ABI_VERSION ||
      api->struct_size < sizeof(CockpitAsrPluginApi) || api->name == nullptr ||
      api->name[0] == '\0' || api->create == nullptr || api->destroy == nullptr ||
      api->recognize == nullptr) {
    *error = "invalid ASR plugin API in " + library_path;
    dlclose(handle);
    return nullptr;
  }

  std::array<char, kErrorCapacity> plugin_error{};
  void* instance = api->create(config_path.c_str(), plugin_error.data(), plugin_error.size());
  if (instance == nullptr) {
    *error = plugin_error[0] == '\0' ? "ASR plugin initialization failed" : plugin_error.data();
    dlclose(handle);
    return nullptr;
  }

  return std::unique_ptr<PluginSpeechRecognizer>(new PluginSpeechRecognizer(handle, api, instance));
}

PluginSpeechRecognizer::PluginSpeechRecognizer(void* handle, const CockpitAsrPluginApi* api,
                                               void* instance)
    : handle_(handle), api_(api), instance_(instance) {
}

PluginSpeechRecognizer::~PluginSpeechRecognizer() {
  api_->destroy(instance_);
  instance_ = nullptr;
  api_ = nullptr;
  // A successfully invoked plugin stays mapped until process exit. Third-party
  // runtimes may keep TLS, worker threads, or static state after destroy().
  handle_ = nullptr;
}

SpeechRecognitionResult PluginSpeechRecognizer::Recognize(const audio::SpeechSegment& segment) {
  const std::string provider = api_->name;
  if (segment.samples.empty()) {
    return {false, {}, provider, 0.0F, "speech segment is empty"};
  }

  uint32_t flags = 0;
  if (segment.truncated) {
    flags |= COCKPIT_ASR_INPUT_TRUNCATED;
  }
  if (segment.discontinuous) {
    flags |= COCKPIT_ASR_INPUT_DISCONTINUOUS;
  }
  const CockpitAsrInput input{
      sizeof(CockpitAsrInput),
      flags,
      audio::AudioFrame::kSampleRateHz,
      audio::AudioFrame::kChannels,
      segment.samples.size(),
      segment.samples.data(),
      segment.start_time_ns,
      segment.end_time_ns,
  };
  std::array<char, kTranscriptCapacity> transcript{};
  std::array<char, kErrorCapacity> recognition_error{};
  CockpitAsrOutput output{
      sizeof(CockpitAsrOutput), 0,    transcript.data(),
      transcript.size(),        0.0F, recognition_error.data(),
      recognition_error.size(),
  };

  const int32_t result = api_->recognize(instance_, &input, &output);
  transcript.back() = '\0';
  recognition_error.back() = '\0';
  if (result != 0) {
    const std::string message =
        recognition_error[0] == '\0' ? "ASR plugin recognition failed" : recognition_error.data();
    return {false, {}, provider, 0.0F, message};
  }
  if (transcript[0] == '\0') {
    return {false, {}, provider, 0.0F, "ASR plugin returned empty text"};
  }
  return {true, transcript.data(), provider, output.confidence, {}};
}

}  // namespace voice
}  // namespace cockpit
