#include "cockpit/core/config/system_config.h"

#include <iostream>
#include <stdexcept>
#include <string>

int main() {
  const auto config = cockpit::config::SystemConfig::LoadFromFile(VALID_CONFIG_PATH);
  if (config.system().name != "cockpit-system" || config.system().vehicle_id != "car_001" ||
      config.services().vehicle_data.grpc.listen_address != "127.0.0.1:50050" ||
      config.services().gateway.stream_timeout_ms != 10000 ||
      config.services().audio.grpc.listen_address != "127.0.0.1:50052" ||
      config.services().audio.vad.speech_start_frames != 3 ||
      config.services().audio.speech_segment.max_segment_ms != 15000 ||
      config.services().camera.capture_backend != "gstreamer" ||
      config.services().camera.preview_stale_timeout_ms != 2000 ||
      config.services().camera.synthetic_fault != "none" ||
      config.services().camera.synthetic_fault_after_frames != 30 ||
      config.services().voice_interaction.grpc.listen_address != "127.0.0.1:50053" ||
      config.services().voice_interaction.gateway_address != "127.0.0.1:50051" ||
      config.hardware().can.interface != "vcan0" ||
      config.features().voice.asr_provider != "mock" ||
      config.features().voice.asr_language != "zh" || config.features().voice.asr_threads != 4 ||
      config.tools().topic.backend != "file" || config.runtime().dependencies.size() != 5 ||
      config.runtime().dependencies[1].required.size() != 2 ||
      config.runtime().dependencies[1].optional.size() != 1) {
    std::cerr << "typed config fields do not match config.yaml" << std::endl;
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
    cockpit::config::SystemConfig::LoadFromFile(INVALID_VAD_CONFIG_PATH);
    std::cerr << "invalid VAD config was accepted" << std::endl;
    return 1;
  } catch (const std::runtime_error& error) {
    const std::string message = error.what();
    if (message.find("services.audio.vad.speech_threshold_dbfs") == std::string::npos) {
      std::cerr << "invalid VAD error did not identify YAML path: " << message << std::endl;
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

  try {
    cockpit::config::SystemConfig::LoadFromFile(INVALID_VOICE_CONFIG_PATH);
    std::cerr << "invalid voice config was accepted" << std::endl;
    return 1;
  } catch (const std::runtime_error& error) {
    const std::string message = error.what();
    if (message.find("features.voice.asr_provider") == std::string::npos) {
      std::cerr << "invalid voice error did not identify config path: " << message << std::endl;
      return 1;
    }
  }
  return 0;
}
