#include "cockpit/core/config/system_config.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

std::string ReadFile(const std::string& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool ReplaceOnce(std::string* text, const std::string& from, const std::string& to) {
  const std::size_t position = text->find(from);
  if (position == std::string::npos) {
    return false;
  }
  text->replace(position, from.size(), to);
  return true;
}

bool ExpectRejectedConfig(const std::string& content, const std::string& expected_message) {
  const std::string path = "/tmp/cockpit-system-config-test.yaml";
  {
    std::ofstream output(path);
    output << content;
  }
  try {
    cockpit::config::SystemConfig::LoadFromFile(path);
    std::cerr << "invalid generated config was accepted\n";
    return false;
  } catch (const std::runtime_error& error) {
    const std::string message = error.what();
    if (message.find(expected_message) == std::string::npos) {
      std::cerr << "generated config error did not contain '" << expected_message
                << "': " << message << std::endl;
      return false;
    }
  }
  return true;
}

}  // namespace

int main() {
  unsetenv("COCKPIT_RUNTIME_DIR");
  const auto config = cockpit::config::SystemConfig::LoadFromFile(VALID_CONFIG_PATH);
  if (config.system().name != "cockpit-system" || config.system().vehicle_id != "car_001" ||
      config.paths().run_dir != "run" || config.logging().dump_time_secs != 5 ||
      config.logging().cut_off_time_mins != 5 || config.logging().max_files != 10 ||
      config.services().vehicle_data.grpc.listen_address != "127.0.0.1:50050" ||
      config.services().gateway.stream_timeout_ms != 10000 ||
      config.services().audio.grpc.listen_address != "127.0.0.1:50052" ||
      config.features().voice.kws.provider != "mock" ||
      config.features().voice.kws.cooldown_ms != 1500 ||
      config.features().voice.kws.wake_word != "你好小车" ||
      config.features().voice.kws.model_dir != "" ||
      config.features().voice.vad.provider != "mock" ||
      config.features().voice.speech_segment.max_segment_ms != 15000 ||
      config.services().camera.capture_backend != "gstreamer" ||
      config.services().camera.preview_stale_timeout_ms != 2000 ||
      config.services().camera.synthetic_fault != "none" ||
      config.services().camera.synthetic_fault_after_frames != 30 ||
      config.services().voice_interaction.grpc.listen_address != "127.0.0.1:50053" ||
      config.services().voice_interaction.gateway_address != "127.0.0.1:50051" ||
      config.services().recording.max_session_bytes != 1073741824ULL ||
      config.services().recording.max_session_duration_seconds != 14400 ||
      config.services().recording.min_free_bytes != 536870912ULL ||
      config.hardware().can.interface != "vcan0" ||
      config.features().voice.asr.provider != "mock" ||
      config.features().ai.asr_timeout_ms != 3000 ||
      config.features().ai.assistant_timeout_ms != 10000 ||
      config.features().ai.command_execution_timeout_ms != 3000 ||
      config.features().ai.tts_synthesis_timeout_ms != 5000 ||
      config.features().ai.follow_up_window_ms != 8000 || config.features().ai.local_llm.enabled ||
      config.features().ai.local_llm.provider != "disabled" ||
      config.features().ai.local_llm.manage_process ||
      config.features().ai.local_llm.port != 8080 ||
      config.features().ai.local_llm.model != "Qwen3.5-2B" ||
      config.features().ai.local_llm.first_token_timeout_ms != 5000 ||
      config.features().ai.local_llm.response_timeout_ms != 30000 ||
      config.features().ai.local_llm.context_size != 2048 ||
      config.tools().topic.backend != "file") {
    std::cerr << "typed config fields do not match config.yaml" << std::endl;
    return 1;
  }

  setenv("COCKPIT_RUNTIME_DIR", "/tmp/cockpit-runtime", 1);
  const auto development_config = cockpit::config::SystemConfig::LoadFromFile(VALID_CONFIG_PATH);
  unsetenv("COCKPIT_RUNTIME_DIR");
  if (development_config.paths().data_dir != "/tmp/cockpit-runtime/data" ||
      development_config.paths().log_dir != "/tmp/cockpit-runtime/logs" ||
      development_config.paths().run_dir != "/tmp/cockpit-runtime/run" ||
      development_config.tools().topic.dir != "/tmp/cockpit-runtime/logs/topics") {
    std::cerr << "COCKPIT_RUNTIME_DIR did not override development output paths" << std::endl;
    return 1;
  }

  const auto production_config =
      cockpit::config::SystemConfig::LoadFromFile(PRODUCTION_CONFIG_PATH);
  if (production_config.services().gateway.grpc.listen_address !=
          "unix:/cockpit-system/run/gateway.grpc.sock" ||
      production_config.services().camera.grpc.listen_address !=
          "unix:/cockpit-system/run/camera.grpc.sock" ||
      production_config.paths().run_dir != "/cockpit-system/run" ||
      production_config.features().voice.kws.provider != "sherpa" ||
      production_config.features().voice.kws.wake_word != "" ||
      production_config.features().voice.kws.keywords_file !=
          "/cockpit-system/ai/config/kws-keywords.txt") {
    std::cerr << "production Unix socket config was not parsed correctly" << std::endl;
    return 1;
  }

  std::string sherpa_with_raw_wake_word = ReadFile(PRODUCTION_CONFIG_PATH);
  if (!ReplaceOnce(&sherpa_with_raw_wake_word, "wake_word: \"\"", "wake_word: 你好小车") ||
      !ExpectRejectedConfig(sherpa_with_raw_wake_word, "wake_word must be empty for sherpa KWS")) {
    return 1;
  }

  std::string enabled_sherpa_missing_model = ReadFile(PRODUCTION_CONFIG_PATH);
  if (!ReplaceOnce(&enabled_sherpa_missing_model, "      enabled: false\n      provider: sherpa",
                   "      enabled: true\n      provider: sherpa") ||
      !ReplaceOnce(&enabled_sherpa_missing_model,
                   "model_dir: /cockpit-system/ai/models/kws/"
                   "sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20",
                   "model_dir: \"\"") ||
      !ExpectRejectedConfig(enabled_sherpa_missing_model,
                            "features.voice.kws.model_dir is required for sherpa KWS")) {
    return 1;
  }

  std::string enabled_llm_without_provider = ReadFile(VALID_CONFIG_PATH);
  if (!ReplaceOnce(&enabled_llm_without_provider, "    local_llm:\n      enabled: false",
                   "    local_llm:\n      enabled: true") ||
      !ExpectRejectedConfig(enabled_llm_without_provider,
                            "features.ai.local_llm.provider must not be disabled")) {
    return 1;
  }

  std::string invalid_llm_temperature = ReadFile(VALID_CONFIG_PATH);
  if (!ReplaceOnce(&invalid_llm_temperature, "      temperature: 0.2", "      temperature: 2.1") ||
      !ExpectRejectedConfig(invalid_llm_temperature,
                            "features.ai.local_llm.temperature must be between 0 and 2")) {
    return 1;
  }

  std::string remote_llm_host = ReadFile(VALID_CONFIG_PATH);
  if (!ReplaceOnce(&remote_llm_host, "      enabled: false\n      provider: disabled",
                   "      enabled: true\n      provider: llama-server") ||
      !ReplaceOnce(&remote_llm_host, "      host: 127.0.0.1", "      host: llm.example.com") ||
      !ExpectRejectedConfig(remote_llm_host,
                            "features.ai.local_llm.host must be a loopback address")) {
    return 1;
  }

  std::string managed_llm_without_paths = ReadFile(VALID_CONFIG_PATH);
  if (!ReplaceOnce(&managed_llm_without_paths,
                   "    local_llm:\n      enabled: false\n      provider: disabled\n"
                   "      manage_process: false",
                   "    local_llm:\n      enabled: true\n      provider: llama-server\n"
                   "      manage_process: true") ||
      !ReplaceOnce(&managed_llm_without_paths,
                   "      executable: _output/ai/runtime/llama.cpp/current/bin/llama-server",
                   "      executable: \"\"") ||
      !ExpectRejectedConfig(managed_llm_without_paths,
                            "features.ai.local_llm.executable must not be empty")) {
    return 1;
  }

  std::string unmanaged_llama_server = ReadFile(VALID_CONFIG_PATH);
  if (!ReplaceOnce(&unmanaged_llama_server,
                   "    local_llm:\n      enabled: false\n      provider: disabled",
                   "    local_llm:\n      enabled: true\n      provider: llama-server") ||
      !ExpectRejectedConfig(unmanaged_llama_server,
                            "features.ai.local_llm.manage_process must be true")) {
    return 1;
  }

  std::string invalid_llm_deadlines = ReadFile(VALID_CONFIG_PATH);
  if (!ReplaceOnce(&invalid_llm_deadlines, "      first_token_timeout_ms: 5000",
                   "      first_token_timeout_ms: 31000") ||
      !ExpectRejectedConfig(invalid_llm_deadlines,
                            "first_token_timeout_ms must not exceed response_timeout_ms")) {
    return 1;
  }

  try {
    cockpit::config::SystemConfig::LoadFromFile(INVALID_CONFIG_PATH);
    std::cerr << "invalid config was accepted" << std::endl;
    return 1;
  } catch (const std::runtime_error& error) {
    const std::string message = error.what();
    if (message.find("services.vehicle_data.grpc.listen_address") == std::string::npos) {
      std::cerr << "invalid config error did not identify YAML path: " << message << std::endl;
      return 1;
    }
  }

  try {
    cockpit::config::SystemConfig::LoadFromFile(INVALID_CONFIG_TYPE_PATH);
    std::cerr << "invalid config type was accepted" << std::endl;
    return 1;
  } catch (const std::runtime_error& error) {
    const std::string message = error.what();
    if (message.find("logging.dump_time_secs must be a scalar") == std::string::npos) {
      std::cerr << "invalid type error did not identify YAML path: " << message << std::endl;
      return 1;
    }
  }

  try {
    cockpit::config::SystemConfig::LoadFromFile(INVALID_CONFIG_KEY_PATH);
    std::cerr << "unknown config key was accepted" << std::endl;
    return 1;
  } catch (const std::runtime_error& error) {
    const std::string message = error.what();
    if (message.find("services.gateway.max_sessions is not supported") == std::string::npos) {
      std::cerr << "unknown key error did not identify YAML path: " << message << std::endl;
      return 1;
    }
  }

  try {
    cockpit::config::SystemConfig::LoadFromFile(INVALID_AUDIO_CONFIG_PATH);
    std::cerr << "invalid audio config was accepted" << std::endl;
    return 1;
  } catch (const std::runtime_error& error) {
    const std::string message = error.what();
    if (message.find("hardware.audio.capture_backend") == std::string::npos) {
      std::cerr << "invalid audio config error did not identify YAML path: " << message
                << std::endl;
      return 1;
    }
  }

  try {
    cockpit::config::SystemConfig::LoadFromFile(INVALID_AUDIO_FORMAT_CONFIG_PATH);
    std::cerr << "unsupported audio format was accepted" << std::endl;
    return 1;
  } catch (const std::runtime_error& error) {
    const std::string message = error.what();
    if (message.find("hardware.audio.sample_rate_hz") == std::string::npos) {
      std::cerr << "invalid audio format error did not identify config path: " << message
                << std::endl;
      return 1;
    }
  }

  for (const auto* path : {INVALID_ZERO_VOICE_TIMEOUT_PATH, INVALID_NEGATIVE_VOICE_TIMEOUT_PATH}) {
    try {
      cockpit::config::SystemConfig::LoadFromFile(path);
      std::cerr << "non-positive voice timeout was accepted: " << path << std::endl;
      return 1;
    } catch (const std::runtime_error& error) {
      const std::string message = error.what();
      if (message.find("must be positive") == std::string::npos &&
          message.find("must be greater than zero") == std::string::npos) {
        std::cerr << "voice timeout validation was not specific: " << error.what() << std::endl;
        return 1;
      }
    }
  }

  try {
    cockpit::config::SystemConfig::LoadFromFile(INVALID_LEGACY_REQUEST_TIMEOUT_PATH);
    std::cerr << "legacy request_timeout_ms was accepted" << std::endl;
    return 1;
  } catch (const std::runtime_error& error) {
    if (std::string(error.what()).find("features.ai.request_timeout_ms is not supported") ==
        std::string::npos) {
      std::cerr << "legacy timeout error did not identify the field: " << error.what() << std::endl;
      return 1;
    }
  }

  return 0;
}
