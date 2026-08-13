#include "cockpit/core/config/system_config.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

int main() {
  unsetenv("COCKPIT_RUNTIME_DIR");
  const auto config = cockpit::config::SystemConfig::LoadFromFile(VALID_CONFIG_PATH);
  if (config.system().name != "cockpit-system" || config.system().vehicle_id != "car_001" ||
      config.paths().run_dir != "run" || config.logging().dump_time_secs != 5 ||
      config.logging().cut_off_time_mins != 5 || config.logging().max_files != 10 ||
      config.services().vehicle_data.grpc.listen_address != "127.0.0.1:50050" ||
      config.services().gateway.stream_timeout_ms != 10000 ||
      config.services().audio.grpc.listen_address != "127.0.0.1:50052" ||
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
      config.features().ai.request_timeout_ms != 10000 ||
      config.features().ai.follow_up_window_ms != 8000 || config.tools().topic.backend != "file") {
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
      production_config.paths().run_dir != "/cockpit-system/run") {
    std::cerr << "production Unix socket config was not parsed correctly" << std::endl;
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

  return 0;
}
