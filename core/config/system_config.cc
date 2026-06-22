#include "core/config/system_config.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

namespace cockpit {
namespace config {
namespace {

YAML::Node ChildMap(const YAML::Node& parent, const std::string& key, const std::string& path) {
  const YAML::Node child = parent[key];
  if (!child) {
    return YAML::Node(YAML::NodeType::Map);
  }
  if (!child.IsMap()) {
    throw std::runtime_error(path + " must be a map");
  }
  return child;
}

template <typename T>
T Read(const YAML::Node& parent,
       const std::string& key,
       const T& default_value,
       const std::string& path) {
  const YAML::Node value = parent[key];
  if (!value) {
    return default_value;
  }
  if (!value.IsScalar()) {
    throw std::runtime_error(path + " must be a scalar");
  }
  try {
    return value.as<T>();
  } catch (const YAML::Exception& error) {
    throw std::runtime_error(path + " has invalid value: " + error.msg);
  }
}

void RequireNotEmpty(const std::string& value, const std::string& path) {
  if (value.empty()) {
    throw std::runtime_error(path + " must not be empty");
  }
}

void RequirePositive(int value, const std::string& path) {
  if (value <= 0) {
    throw std::runtime_error(path + " must be greater than zero");
  }
}

void ValidateAddress(const std::string& value, const std::string& path) {
  const std::size_t separator = value.rfind(':');
  if (separator == std::string::npos || separator == 0 || separator + 1 >= value.size()) {
    throw std::runtime_error(path + " must use host:port format");
  }
  try {
    const std::string port_text = value.substr(separator + 1);
    std::size_t consumed = 0;
    const int port = std::stoi(port_text, &consumed);
    if (consumed != port_text.size()) {
      throw std::invalid_argument("trailing port characters");
    }
    if (port < 1 || port > 65535) {
      throw std::runtime_error(path + " port must be between 1 and 65535");
    }
  } catch (const std::invalid_argument&) {
    throw std::runtime_error(path + " port must be an integer");
  } catch (const std::out_of_range&) {
    throw std::runtime_error(path + " port is out of range");
  }
}

bool IsOneOf(const std::string& value,
             const std::string& first,
             const std::string& second) {
  return value == first || value == second;
}

}  // namespace

SystemConfig SystemConfig::LoadFromFile(const std::string& path) {
  YAML::Node root;
  try {
    root = YAML::LoadFile(path);
  } catch (const YAML::Exception& error) {
    throw std::runtime_error("failed to load config file " + path + ": " + error.msg);
  }
  if (!root.IsMap()) {
    throw std::runtime_error("config root must be a map: " + path);
  }

  SystemConfig config;
  const YAML::Node system = ChildMap(root, "system", "system");
  config.system_.name = Read(system, "name", config.system_.name, "system.name");
  config.system_.vehicle_id =
      Read(system, "vehicle_id", config.system_.vehicle_id, "system.vehicle_id");

  const YAML::Node paths = ChildMap(root, "paths", "paths");
  config.paths_.data_dir = Read(paths, "data_dir", config.paths_.data_dir, "paths.data_dir");
  config.paths_.log_dir = Read(paths, "log_dir", config.paths_.log_dir, "paths.log_dir");

  const YAML::Node logging = ChildMap(root, "logging", "logging");
  config.logging_.level = Read(logging, "level", config.logging_.level, "logging.level");
  config.logging_.max_bytes =
      Read(logging, "max_bytes", config.logging_.max_bytes, "logging.max_bytes");
  config.logging_.mirror_stderr = Read(
      logging, "mirror_stderr", config.logging_.mirror_stderr, "logging.mirror_stderr");

  const YAML::Node services = ChildMap(root, "services", "services");
  const YAML::Node vehicle_data =
      ChildMap(services, "vehicle_data", "services.vehicle_data");
  config.services_.vehicle_data.source = Read(
      vehicle_data, "source", config.services_.vehicle_data.source, "services.vehicle_data.source");
  config.services_.vehicle_data.publish_interval_ms =
      Read(vehicle_data,
           "publish_interval_ms",
           config.services_.vehicle_data.publish_interval_ms,
           "services.vehicle_data.publish_interval_ms");
  const YAML::Node vehicle_grpc =
      ChildMap(vehicle_data, "grpc", "services.vehicle_data.grpc");
  config.services_.vehicle_data.grpc.listen_address =
      Read(vehicle_grpc,
           "listen_address",
           config.services_.vehicle_data.grpc.listen_address,
           "services.vehicle_data.grpc.listen_address");

  const YAML::Node gateway = ChildMap(services, "gateway", "services.gateway");
  config.services_.gateway.vehicle_data_address =
      Read(gateway,
           "vehicle_data_address",
           config.services_.gateway.vehicle_data_address,
           "services.gateway.vehicle_data_address");
  config.services_.gateway.stream_timeout_ms =
      Read(gateway,
           "stream_timeout_ms",
           config.services_.gateway.stream_timeout_ms,
           "services.gateway.stream_timeout_ms");
  config.services_.gateway.retry_delay_ms =
      Read(gateway,
           "retry_delay_ms",
           config.services_.gateway.retry_delay_ms,
           "services.gateway.retry_delay_ms");
  const YAML::Node gateway_grpc = ChildMap(gateway, "grpc", "services.gateway.grpc");
  config.services_.gateway.grpc.listen_address =
      Read(gateway_grpc,
           "listen_address",
           config.services_.gateway.grpc.listen_address,
           "services.gateway.grpc.listen_address");
  const YAML::Node gateway_websocket =
      ChildMap(gateway, "websocket", "services.gateway.websocket");
  config.services_.gateway.websocket.listen_address =
      Read(gateway_websocket,
           "listen_address",
           config.services_.gateway.websocket.listen_address,
           "services.gateway.websocket.listen_address");

  const YAML::Node audio_service = ChildMap(services, "audio", "services.audio");
  config.services_.audio.auto_start =
      Read(audio_service,
           "auto_start",
           config.services_.audio.auto_start,
           "services.audio.auto_start");
  const YAML::Node audio_grpc =
      ChildMap(audio_service, "grpc", "services.audio.grpc");
  config.services_.audio.grpc.listen_address =
      Read(audio_grpc,
           "listen_address",
           config.services_.audio.grpc.listen_address,
           "services.audio.grpc.listen_address");
  const YAML::Node vad = ChildMap(audio_service, "vad", "services.audio.vad");
  config.services_.audio.vad.enabled =
      Read(vad, "enabled", config.services_.audio.vad.enabled,
           "services.audio.vad.enabled");
  config.services_.audio.vad.backend =
      Read(vad, "backend", config.services_.audio.vad.backend,
           "services.audio.vad.backend");
  config.services_.audio.vad.speech_threshold_dbfs =
      Read(vad, "speech_threshold_dbfs",
           config.services_.audio.vad.speech_threshold_dbfs,
           "services.audio.vad.speech_threshold_dbfs");
  config.services_.audio.vad.speech_start_frames =
      Read(vad, "speech_start_frames",
           config.services_.audio.vad.speech_start_frames,
           "services.audio.vad.speech_start_frames");
  config.services_.audio.vad.speech_end_frames =
      Read(vad, "speech_end_frames",
           config.services_.audio.vad.speech_end_frames,
           "services.audio.vad.speech_end_frames");
  const YAML::Node speech_segment =
      ChildMap(audio_service, "speech_segment", "services.audio.speech_segment");
  config.services_.audio.speech_segment.pre_roll_ms =
      Read(speech_segment, "pre_roll_ms",
           config.services_.audio.speech_segment.pre_roll_ms,
           "services.audio.speech_segment.pre_roll_ms");
  config.services_.audio.speech_segment.max_segment_ms =
      Read(speech_segment, "max_segment_ms",
           config.services_.audio.speech_segment.max_segment_ms,
           "services.audio.speech_segment.max_segment_ms");

  const YAML::Node voice_interaction =
      ChildMap(services, "voice_interaction", "services.voice_interaction");
  config.services_.voice_interaction.audio_address =
      Read(voice_interaction, "audio_address",
           config.services_.voice_interaction.audio_address,
           "services.voice_interaction.audio_address");
  config.services_.voice_interaction.stream_timeout_ms =
      Read(voice_interaction, "stream_timeout_ms",
           config.services_.voice_interaction.stream_timeout_ms,
           "services.voice_interaction.stream_timeout_ms");
  config.services_.voice_interaction.retry_delay_ms =
      Read(voice_interaction, "retry_delay_ms",
           config.services_.voice_interaction.retry_delay_ms,
           "services.voice_interaction.retry_delay_ms");
  const YAML::Node voice_grpc = ChildMap(
      voice_interaction, "grpc", "services.voice_interaction.grpc");
  config.services_.voice_interaction.grpc.listen_address =
      Read(voice_grpc, "listen_address",
           config.services_.voice_interaction.grpc.listen_address,
           "services.voice_interaction.grpc.listen_address");

  const YAML::Node cloud_uplink =
      ChildMap(services, "cloud_uplink", "services.cloud_uplink");
  config.services_.cloud_uplink.enabled = Read(
      cloud_uplink, "enabled", config.services_.cloud_uplink.enabled, "services.cloud_uplink.enabled");
  const YAML::Node mqtt = ChildMap(cloud_uplink, "mqtt", "services.cloud_uplink.mqtt");
  config.services_.cloud_uplink.mqtt.broker =
      Read(mqtt, "broker", config.services_.cloud_uplink.mqtt.broker,
           "services.cloud_uplink.mqtt.broker");
  config.services_.cloud_uplink.mqtt.telemetry_topic =
      Read(mqtt,
           "telemetry_topic",
           config.services_.cloud_uplink.mqtt.telemetry_topic,
           "services.cloud_uplink.mqtt.telemetry_topic");
  config.services_.cloud_uplink.mqtt.qos =
      Read(mqtt, "qos", config.services_.cloud_uplink.mqtt.qos, "services.cloud_uplink.mqtt.qos");

  const YAML::Node hardware = ChildMap(root, "hardware", "hardware");
  const YAML::Node can = ChildMap(hardware, "can", "hardware.can");
  config.hardware_.can.interface =
      Read(can, "interface", config.hardware_.can.interface, "hardware.can.interface");
  config.hardware_.can.simulator_backend =
      Read(can,
           "simulator_backend",
           config.hardware_.can.simulator_backend,
           "hardware.can.simulator_backend");
  config.hardware_.can.simulator_interval_ms =
      Read(can,
           "simulator_interval_ms",
           config.hardware_.can.simulator_interval_ms,
           "hardware.can.simulator_interval_ms");
  config.hardware_.can.receive_timeout_ms =
      Read(can,
           "receive_timeout_ms",
           config.hardware_.can.receive_timeout_ms,
           "hardware.can.receive_timeout_ms");
  config.hardware_.can.max_idle_timeouts =
      Read(can,
           "max_idle_timeouts",
           config.hardware_.can.max_idle_timeouts,
           "hardware.can.max_idle_timeouts");

  const YAML::Node audio = ChildMap(hardware, "audio", "hardware.audio");
  config.hardware_.audio.capture_backend =
      Read(audio,
           "capture_backend",
           config.hardware_.audio.capture_backend,
           "hardware.audio.capture_backend");
  config.hardware_.audio.playback_backend =
      Read(audio,
           "playback_backend",
           config.hardware_.audio.playback_backend,
           "hardware.audio.playback_backend");
  config.hardware_.audio.input_device =
      Read(audio, "input_device", config.hardware_.audio.input_device, "hardware.audio.input_device");
  config.hardware_.audio.output_device =
      Read(audio,
           "output_device",
           config.hardware_.audio.output_device,
           "hardware.audio.output_device");
  config.hardware_.audio.sample_rate_hz =
      Read(audio,
           "sample_rate_hz",
           config.hardware_.audio.sample_rate_hz,
           "hardware.audio.sample_rate_hz");
  config.hardware_.audio.channels =
      Read(audio, "channels", config.hardware_.audio.channels, "hardware.audio.channels");
  config.hardware_.audio.frame_ms =
      Read(audio, "frame_ms", config.hardware_.audio.frame_ms, "hardware.audio.frame_ms");

  const YAML::Node features = ChildMap(root, "features", "features");
  const YAML::Node voice = ChildMap(features, "voice", "features.voice");
  config.features_.voice.enabled =
      Read(voice, "enabled", config.features_.voice.enabled, "features.voice.enabled");
  config.features_.voice.mode =
      Read(voice, "mode", config.features_.voice.mode, "features.voice.mode");
  config.features_.voice.asr_provider =
      Read(voice,
           "asr_provider",
           config.features_.voice.asr_provider,
           "features.voice.asr_provider");
  config.features_.voice.tts_provider =
      Read(voice,
           "tts_provider",
           config.features_.voice.tts_provider,
           "features.voice.tts_provider");
  const YAML::Node ai = ChildMap(features, "ai", "features.ai");
  config.features_.ai.provider =
      Read(ai, "provider", config.features_.ai.provider, "features.ai.provider");
  config.features_.ai.model = Read(ai, "model", config.features_.ai.model, "features.ai.model");
  config.features_.ai.request_timeout_ms =
      Read(ai,
           "request_timeout_ms",
           config.features_.ai.request_timeout_ms,
           "features.ai.request_timeout_ms");

  const YAML::Node tools = ChildMap(root, "tools", "tools");
  const YAML::Node topic = ChildMap(tools, "topic", "tools.topic");
  config.tools_.topic.backend =
      Read(topic, "backend", config.tools_.topic.backend, "tools.topic.backend");
  config.tools_.topic.dir = Read(topic, "dir", config.tools_.topic.dir, "tools.topic.dir");

  config.Validate();
  return config;
}

void SystemConfig::Validate() const {
  RequireNotEmpty(system_.name, "system.name");
  RequireNotEmpty(system_.vehicle_id, "system.vehicle_id");
  RequireNotEmpty(paths_.data_dir, "paths.data_dir");
  RequireNotEmpty(paths_.log_dir, "paths.log_dir");
  RequirePositive(logging_.max_bytes, "logging.max_bytes");
  if (logging_.level != "debug" && logging_.level != "info" && logging_.level != "warn" &&
      logging_.level != "warning" && logging_.level != "error") {
    throw std::runtime_error("logging.level must be debug, info, warn, warning, or error");
  }

  if (!IsOneOf(services_.vehicle_data.source, "mock", "socketcan")) {
    throw std::runtime_error("services.vehicle_data.source must be mock or socketcan");
  }
  RequirePositive(services_.vehicle_data.publish_interval_ms,
                  "services.vehicle_data.publish_interval_ms");
  ValidateAddress(services_.vehicle_data.grpc.listen_address,
                  "services.vehicle_data.grpc.listen_address");
  ValidateAddress(services_.gateway.vehicle_data_address,
                  "services.gateway.vehicle_data_address");
  ValidateAddress(services_.gateway.grpc.listen_address,
                  "services.gateway.grpc.listen_address");
  ValidateAddress(services_.gateway.websocket.listen_address,
                  "services.gateway.websocket.listen_address");
  RequirePositive(services_.gateway.stream_timeout_ms, "services.gateway.stream_timeout_ms");
  RequirePositive(services_.gateway.retry_delay_ms, "services.gateway.retry_delay_ms");
  ValidateAddress(services_.audio.grpc.listen_address,
                  "services.audio.grpc.listen_address");
  if (services_.audio.vad.backend != "energy") {
    throw std::runtime_error("services.audio.vad.backend currently supports only energy");
  }
  if (services_.audio.vad.speech_threshold_dbfs < -100.0 ||
      services_.audio.vad.speech_threshold_dbfs > 0.0) {
    throw std::runtime_error(
        "services.audio.vad.speech_threshold_dbfs must be between -100 and 0");
  }
  RequirePositive(services_.audio.vad.speech_start_frames,
                  "services.audio.vad.speech_start_frames");
  RequirePositive(services_.audio.vad.speech_end_frames,
                  "services.audio.vad.speech_end_frames");
  if (services_.audio.speech_segment.pre_roll_ms < 0) {
    throw std::runtime_error(
        "services.audio.speech_segment.pre_roll_ms must not be negative");
  }
  RequirePositive(services_.audio.speech_segment.max_segment_ms,
                  "services.audio.speech_segment.max_segment_ms");
  if (services_.audio.speech_segment.pre_roll_ms > 2000) {
    throw std::runtime_error(
        "services.audio.speech_segment.pre_roll_ms must not exceed 2000");
  }
  if (services_.audio.speech_segment.max_segment_ms > 60000) {
    throw std::runtime_error(
        "services.audio.speech_segment.max_segment_ms must not exceed 60000");
  }
  if (services_.audio.speech_segment.pre_roll_ms >=
      services_.audio.speech_segment.max_segment_ms) {
    throw std::runtime_error(
        "services.audio.speech_segment.pre_roll_ms must be less than max_segment_ms");
  }
  ValidateAddress(services_.voice_interaction.audio_address,
                  "services.voice_interaction.audio_address");
  ValidateAddress(services_.voice_interaction.grpc.listen_address,
                  "services.voice_interaction.grpc.listen_address");
  RequirePositive(services_.voice_interaction.stream_timeout_ms,
                  "services.voice_interaction.stream_timeout_ms");
  RequirePositive(services_.voice_interaction.retry_delay_ms,
                  "services.voice_interaction.retry_delay_ms");

  RequireNotEmpty(services_.cloud_uplink.mqtt.broker, "services.cloud_uplink.mqtt.broker");
  RequireNotEmpty(services_.cloud_uplink.mqtt.telemetry_topic,
                  "services.cloud_uplink.mqtt.telemetry_topic");
  if (services_.cloud_uplink.mqtt.qos < 0 || services_.cloud_uplink.mqtt.qos > 2) {
    throw std::runtime_error("services.cloud_uplink.mqtt.qos must be between 0 and 2");
  }

  RequireNotEmpty(hardware_.can.interface, "hardware.can.interface");
  if (!IsOneOf(hardware_.can.simulator_backend, "stdout", "socketcan")) {
    throw std::runtime_error("hardware.can.simulator_backend must be stdout or socketcan");
  }
  RequirePositive(hardware_.can.simulator_interval_ms, "hardware.can.simulator_interval_ms");
  RequirePositive(hardware_.can.receive_timeout_ms, "hardware.can.receive_timeout_ms");
  RequirePositive(hardware_.can.max_idle_timeouts, "hardware.can.max_idle_timeouts");
  RequirePositive(hardware_.audio.sample_rate_hz, "hardware.audio.sample_rate_hz");
  RequirePositive(hardware_.audio.channels, "hardware.audio.channels");
  RequirePositive(hardware_.audio.frame_ms, "hardware.audio.frame_ms");
  if (services_.audio.speech_segment.pre_roll_ms % hardware_.audio.frame_ms != 0) {
    throw std::runtime_error(
        "services.audio.speech_segment.pre_roll_ms must align with hardware.audio.frame_ms");
  }
  if (services_.audio.speech_segment.max_segment_ms % hardware_.audio.frame_ms != 0) {
    throw std::runtime_error(
        "services.audio.speech_segment.max_segment_ms must align with hardware.audio.frame_ms");
  }
  if (hardware_.audio.capture_backend != "alsa") {
    throw std::runtime_error("hardware.audio.capture_backend currently supports only alsa");
  }
  if (hardware_.audio.playback_backend != "alsa") {
    throw std::runtime_error("hardware.audio.playback_backend currently supports only alsa");
  }
  RequireNotEmpty(hardware_.audio.input_device, "hardware.audio.input_device");
  RequireNotEmpty(hardware_.audio.output_device, "hardware.audio.output_device");
  RequirePositive(features_.ai.request_timeout_ms, "features.ai.request_timeout_ms");
  if (features_.voice.mode != "push_to_talk") {
    throw std::runtime_error("features.voice.mode currently supports only push_to_talk");
  }
  if (features_.voice.asr_provider != "mock") {
    throw std::runtime_error("features.voice.asr_provider currently supports only mock");
  }
  if (features_.voice.tts_provider != "mock") {
    throw std::runtime_error("features.voice.tts_provider currently supports only mock");
  }
  if (!IsOneOf(tools_.topic.backend, "file", "grpc")) {
    throw std::runtime_error("tools.topic.backend must be file or grpc");
  }
  RequireNotEmpty(tools_.topic.dir, "tools.topic.dir");
}

}  // namespace config
}  // namespace cockpit
