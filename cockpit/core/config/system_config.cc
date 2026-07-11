#include "cockpit/core/config/system_config.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
T Read(const YAML::Node& parent, const std::string& key, const T& default_value,
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

bool IsOneOf(const std::string& value, const std::string& first, const std::string& second) {
  return value == first || value == second;
}

std::vector<std::string> ReadStringList(const YAML::Node& parent, const std::string& key,
                                        const std::vector<std::string>& default_value,
                                        const std::string& path) {
  const YAML::Node value = parent[key];
  if (!value) {
    return default_value;
  }
  if (!value.IsSequence()) {
    throw std::runtime_error(path + " must be a list");
  }
  std::vector<std::string> result;
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (!value[i].IsScalar()) {
      throw std::runtime_error(path + " items must be scalars");
    }
    result.push_back(value[i].as<std::string>());
  }
  return result;
}

std::vector<ServiceDependencyConfig> ReadServiceDependencies(
    const YAML::Node& parent, const std::vector<ServiceDependencyConfig>& default_value,
    const std::string& path) {
  const YAML::Node value = parent["dependencies"];
  if (!value) {
    return default_value;
  }
  if (!value.IsSequence()) {
    throw std::runtime_error(path + ".dependencies must be a list");
  }
  std::vector<ServiceDependencyConfig> result;
  for (std::size_t i = 0; i < value.size(); ++i) {
    const YAML::Node item = value[i];
    if (!item.IsMap()) {
      throw std::runtime_error(path + ".dependencies items must be maps");
    }
    ServiceDependencyConfig dependency;
    dependency.service = Read(item, "service", dependency.service,
                              path + ".dependencies[" + std::to_string(i) + "].service");
    dependency.required =
        ReadStringList(item, "required", dependency.required,
                       path + ".dependencies[" + std::to_string(i) + "].required");
    dependency.optional =
        ReadStringList(item, "optional", dependency.optional,
                       path + ".dependencies[" + std::to_string(i) + "].optional");
    result.push_back(std::move(dependency));
  }
  return result;
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
  config.logging_.mirror_stderr =
      Read(logging, "mirror_stderr", config.logging_.mirror_stderr, "logging.mirror_stderr");

  const YAML::Node services = ChildMap(root, "services", "services");
  const YAML::Node vehicle_data = ChildMap(services, "vehicle_data", "services.vehicle_data");
  config.services_.vehicle_data.source = Read(
      vehicle_data, "source", config.services_.vehicle_data.source, "services.vehicle_data.source");
  config.services_.vehicle_data.publish_interval_ms =
      Read(vehicle_data, "publish_interval_ms", config.services_.vehicle_data.publish_interval_ms,
           "services.vehicle_data.publish_interval_ms");
  const YAML::Node vehicle_grpc = ChildMap(vehicle_data, "grpc", "services.vehicle_data.grpc");
  config.services_.vehicle_data.grpc.listen_address =
      Read(vehicle_grpc, "listen_address", config.services_.vehicle_data.grpc.listen_address,
           "services.vehicle_data.grpc.listen_address");

  const YAML::Node gateway = ChildMap(services, "gateway", "services.gateway");
  config.services_.gateway.vehicle_data_address =
      Read(gateway, "vehicle_data_address", config.services_.gateway.vehicle_data_address,
           "services.gateway.vehicle_data_address");
  config.services_.gateway.stream_timeout_ms =
      Read(gateway, "stream_timeout_ms", config.services_.gateway.stream_timeout_ms,
           "services.gateway.stream_timeout_ms");
  config.services_.gateway.retry_delay_ms =
      Read(gateway, "retry_delay_ms", config.services_.gateway.retry_delay_ms,
           "services.gateway.retry_delay_ms");
  const YAML::Node gateway_grpc = ChildMap(gateway, "grpc", "services.gateway.grpc");
  config.services_.gateway.grpc.listen_address =
      Read(gateway_grpc, "listen_address", config.services_.gateway.grpc.listen_address,
           "services.gateway.grpc.listen_address");
  const YAML::Node gateway_websocket = ChildMap(gateway, "websocket", "services.gateway.websocket");
  config.services_.gateway.websocket.listen_address =
      Read(gateway_websocket, "listen_address", config.services_.gateway.websocket.listen_address,
           "services.gateway.websocket.listen_address");

  const YAML::Node audio_service = ChildMap(services, "audio", "services.audio");
  config.services_.audio.auto_start = Read(
      audio_service, "auto_start", config.services_.audio.auto_start, "services.audio.auto_start");
  const YAML::Node audio_grpc = ChildMap(audio_service, "grpc", "services.audio.grpc");
  config.services_.audio.grpc.listen_address =
      Read(audio_grpc, "listen_address", config.services_.audio.grpc.listen_address,
           "services.audio.grpc.listen_address");
  const YAML::Node vad = ChildMap(audio_service, "vad", "services.audio.vad");
  config.services_.audio.vad.enabled =
      Read(vad, "enabled", config.services_.audio.vad.enabled, "services.audio.vad.enabled");
  config.services_.audio.vad.backend =
      Read(vad, "backend", config.services_.audio.vad.backend, "services.audio.vad.backend");
  config.services_.audio.vad.speech_threshold_dbfs =
      Read(vad, "speech_threshold_dbfs", config.services_.audio.vad.speech_threshold_dbfs,
           "services.audio.vad.speech_threshold_dbfs");
  config.services_.audio.vad.speech_start_frames =
      Read(vad, "speech_start_frames", config.services_.audio.vad.speech_start_frames,
           "services.audio.vad.speech_start_frames");
  config.services_.audio.vad.speech_end_frames =
      Read(vad, "speech_end_frames", config.services_.audio.vad.speech_end_frames,
           "services.audio.vad.speech_end_frames");
  const YAML::Node speech_segment =
      ChildMap(audio_service, "speech_segment", "services.audio.speech_segment");
  config.services_.audio.speech_segment.pre_roll_ms =
      Read(speech_segment, "pre_roll_ms", config.services_.audio.speech_segment.pre_roll_ms,
           "services.audio.speech_segment.pre_roll_ms");
  config.services_.audio.speech_segment.max_segment_ms =
      Read(speech_segment, "max_segment_ms", config.services_.audio.speech_segment.max_segment_ms,
           "services.audio.speech_segment.max_segment_ms");

  const YAML::Node camera_service = ChildMap(services, "camera", "services.camera");
  const YAML::Node camera_grpc = ChildMap(camera_service, "grpc", "services.camera.grpc");
  config.services_.camera.grpc.listen_address =
      Read(camera_grpc, "listen_address", config.services_.camera.grpc.listen_address,
           "services.camera.grpc.listen_address");
  config.services_.camera.capture_backend =
      Read(camera_service, "capture_backend", config.services_.camera.capture_backend,
           "services.camera.capture_backend");
  config.services_.camera.preview_stale_timeout_ms = Read(
      camera_service, "preview_stale_timeout_ms", config.services_.camera.preview_stale_timeout_ms,
      "services.camera.preview_stale_timeout_ms");
  config.services_.camera.synthetic_fault =
      Read(camera_service, "synthetic_fault", config.services_.camera.synthetic_fault,
           "services.camera.synthetic_fault");
  config.services_.camera.synthetic_fault_after_frames =
      Read(camera_service, "synthetic_fault_after_frames",
           config.services_.camera.synthetic_fault_after_frames,
           "services.camera.synthetic_fault_after_frames");
  config.services_.camera.frame_transport =
      Read(camera_service, "frame_transport", config.services_.camera.frame_transport,
           "services.camera.frame_transport");
  config.services_.camera.shared_memory_name =
      Read(camera_service, "shared_memory_name", config.services_.camera.shared_memory_name,
           "services.camera.shared_memory_name");
  config.services_.camera.max_frame_bytes =
      Read(camera_service, "max_frame_bytes", config.services_.camera.max_frame_bytes,
           "services.camera.max_frame_bytes");
  config.services_.camera.photo_directory =
      Read(camera_service, "photo_directory", config.services_.camera.photo_directory,
           "services.camera.photo_directory");
  config.services_.camera.photo_jpeg_quality =
      Read(camera_service, "photo_jpeg_quality", config.services_.camera.photo_jpeg_quality,
           "services.camera.photo_jpeg_quality");
  config.services_.camera.photo_max_frame_age_ms =
      Read(camera_service, "photo_max_frame_age_ms", config.services_.camera.photo_max_frame_age_ms,
           "services.camera.photo_max_frame_age_ms");

  const YAML::Node voice_interaction =
      ChildMap(services, "voice_interaction", "services.voice_interaction");
  config.services_.voice_interaction.audio_address =
      Read(voice_interaction, "audio_address", config.services_.voice_interaction.audio_address,
           "services.voice_interaction.audio_address");
  config.services_.voice_interaction.gateway_address =
      Read(voice_interaction, "gateway_address", config.services_.voice_interaction.gateway_address,
           "services.voice_interaction.gateway_address");
  config.services_.voice_interaction.stream_timeout_ms = Read(
      voice_interaction, "stream_timeout_ms", config.services_.voice_interaction.stream_timeout_ms,
      "services.voice_interaction.stream_timeout_ms");
  config.services_.voice_interaction.retry_delay_ms =
      Read(voice_interaction, "retry_delay_ms", config.services_.voice_interaction.retry_delay_ms,
           "services.voice_interaction.retry_delay_ms");
  const YAML::Node voice_grpc =
      ChildMap(voice_interaction, "grpc", "services.voice_interaction.grpc");
  config.services_.voice_interaction.grpc.listen_address =
      Read(voice_grpc, "listen_address", config.services_.voice_interaction.grpc.listen_address,
           "services.voice_interaction.grpc.listen_address");

  const YAML::Node cloud_uplink = ChildMap(services, "cloud_uplink", "services.cloud_uplink");
  config.services_.cloud_uplink.enabled =
      Read(cloud_uplink, "enabled", config.services_.cloud_uplink.enabled,
           "services.cloud_uplink.enabled");
  const YAML::Node mqtt = ChildMap(cloud_uplink, "mqtt", "services.cloud_uplink.mqtt");
  config.services_.cloud_uplink.mqtt.broker =
      Read(mqtt, "broker", config.services_.cloud_uplink.mqtt.broker,
           "services.cloud_uplink.mqtt.broker");
  config.services_.cloud_uplink.mqtt.telemetry_topic =
      Read(mqtt, "telemetry_topic", config.services_.cloud_uplink.mqtt.telemetry_topic,
           "services.cloud_uplink.mqtt.telemetry_topic");
  config.services_.cloud_uplink.mqtt.qos =
      Read(mqtt, "qos", config.services_.cloud_uplink.mqtt.qos, "services.cloud_uplink.mqtt.qos");

  const YAML::Node recording = ChildMap(services, "recording", "services.recording");
  config.services_.recording.auto_start =
      Read(recording, "auto_start", config.services_.recording.auto_start,
           "services.recording.auto_start");
  config.services_.recording.directory = Read(
      recording, "directory", config.services_.recording.directory, "services.recording.directory");
  config.services_.recording.vehicle_data_address =
      Read(recording, "vehicle_data_address", config.services_.recording.vehicle_data_address,
           "services.recording.vehicle_data_address");
  config.services_.recording.stream_timeout_ms =
      Read(recording, "stream_timeout_ms", config.services_.recording.stream_timeout_ms,
           "services.recording.stream_timeout_ms");
  config.services_.recording.retry_delay_ms =
      Read(recording, "retry_delay_ms", config.services_.recording.retry_delay_ms,
           "services.recording.retry_delay_ms");
  config.services_.recording.max_sessions =
      Read(recording, "max_sessions", config.services_.recording.max_sessions,
           "services.recording.max_sessions");
  config.services_.recording.max_total_bytes =
      Read(recording, "max_total_bytes", config.services_.recording.max_total_bytes,
           "services.recording.max_total_bytes");
  const YAML::Node recording_grpc = ChildMap(recording, "grpc", "services.recording.grpc");
  config.services_.recording.grpc.listen_address =
      Read(recording_grpc, "listen_address", config.services_.recording.grpc.listen_address,
           "services.recording.grpc.listen_address");

  const YAML::Node hardware = ChildMap(root, "hardware", "hardware");
  const YAML::Node can = ChildMap(hardware, "can", "hardware.can");
  config.hardware_.can.interface =
      Read(can, "interface", config.hardware_.can.interface, "hardware.can.interface");
  config.hardware_.can.simulator_backend =
      Read(can, "simulator_backend", config.hardware_.can.simulator_backend,
           "hardware.can.simulator_backend");
  config.hardware_.can.simulator_interval_ms =
      Read(can, "simulator_interval_ms", config.hardware_.can.simulator_interval_ms,
           "hardware.can.simulator_interval_ms");
  config.hardware_.can.receive_timeout_ms =
      Read(can, "receive_timeout_ms", config.hardware_.can.receive_timeout_ms,
           "hardware.can.receive_timeout_ms");
  config.hardware_.can.max_idle_timeouts =
      Read(can, "max_idle_timeouts", config.hardware_.can.max_idle_timeouts,
           "hardware.can.max_idle_timeouts");

  const YAML::Node audio = ChildMap(hardware, "audio", "hardware.audio");
  config.hardware_.audio.capture_backend =
      Read(audio, "capture_backend", config.hardware_.audio.capture_backend,
           "hardware.audio.capture_backend");
  config.hardware_.audio.playback_backend =
      Read(audio, "playback_backend", config.hardware_.audio.playback_backend,
           "hardware.audio.playback_backend");
  config.hardware_.audio.input_device = Read(
      audio, "input_device", config.hardware_.audio.input_device, "hardware.audio.input_device");
  config.hardware_.audio.output_device = Read(
      audio, "output_device", config.hardware_.audio.output_device, "hardware.audio.output_device");
  config.hardware_.audio.sample_rate_hz =
      Read(audio, "sample_rate_hz", config.hardware_.audio.sample_rate_hz,
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
  config.features_.voice.asr_provider = Read(
      voice, "asr_provider", config.features_.voice.asr_provider, "features.voice.asr_provider");
  config.features_.voice.asr_model_path =
      Read(voice, "asr_model_path", config.features_.voice.asr_model_path,
           "features.voice.asr_model_path");
  config.features_.voice.asr_language = Read(
      voice, "asr_language", config.features_.voice.asr_language, "features.voice.asr_language");
  config.features_.voice.asr_threads =
      Read(voice, "asr_threads", config.features_.voice.asr_threads, "features.voice.asr_threads");
  config.features_.voice.tts_provider = Read(
      voice, "tts_provider", config.features_.voice.tts_provider, "features.voice.tts_provider");
  const YAML::Node ai = ChildMap(features, "ai", "features.ai");
  config.features_.ai.provider =
      Read(ai, "provider", config.features_.ai.provider, "features.ai.provider");
  config.features_.ai.model = Read(ai, "model", config.features_.ai.model, "features.ai.model");
  config.features_.ai.request_timeout_ms =
      Read(ai, "request_timeout_ms", config.features_.ai.request_timeout_ms,
           "features.ai.request_timeout_ms");

  const YAML::Node tools = ChildMap(root, "tools", "tools");
  const YAML::Node topic = ChildMap(tools, "topic", "tools.topic");
  config.tools_.topic.backend =
      Read(topic, "backend", config.tools_.topic.backend, "tools.topic.backend");
  config.tools_.topic.dir = Read(topic, "dir", config.tools_.topic.dir, "tools.topic.dir");

  const YAML::Node runtime = ChildMap(root, "runtime", "runtime");
  config.runtime_.dependencies =
      ReadServiceDependencies(runtime, config.runtime_.dependencies, "runtime");

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
  ValidateAddress(services_.gateway.vehicle_data_address, "services.gateway.vehicle_data_address");
  ValidateAddress(services_.gateway.grpc.listen_address, "services.gateway.grpc.listen_address");
  ValidateAddress(services_.gateway.websocket.listen_address,
                  "services.gateway.websocket.listen_address");
  RequirePositive(services_.gateway.stream_timeout_ms, "services.gateway.stream_timeout_ms");
  RequirePositive(services_.gateway.retry_delay_ms, "services.gateway.retry_delay_ms");
  ValidateAddress(services_.audio.grpc.listen_address, "services.audio.grpc.listen_address");
  if (services_.audio.vad.backend != "energy") {
    throw std::runtime_error("services.audio.vad.backend currently supports only energy");
  }
  if (services_.audio.vad.speech_threshold_dbfs < -100.0 ||
      services_.audio.vad.speech_threshold_dbfs > 0.0) {
    throw std::runtime_error("services.audio.vad.speech_threshold_dbfs must be between -100 and 0");
  }
  RequirePositive(services_.audio.vad.speech_start_frames,
                  "services.audio.vad.speech_start_frames");
  RequirePositive(services_.audio.vad.speech_end_frames, "services.audio.vad.speech_end_frames");
  if (services_.audio.speech_segment.pre_roll_ms < 0) {
    throw std::runtime_error("services.audio.speech_segment.pre_roll_ms must not be negative");
  }
  RequirePositive(services_.audio.speech_segment.max_segment_ms,
                  "services.audio.speech_segment.max_segment_ms");
  if (services_.audio.speech_segment.pre_roll_ms > 2000) {
    throw std::runtime_error("services.audio.speech_segment.pre_roll_ms must not exceed 2000");
  }
  if (services_.audio.speech_segment.max_segment_ms > 60000) {
    throw std::runtime_error("services.audio.speech_segment.max_segment_ms must not exceed 60000");
  }
  if (services_.audio.speech_segment.pre_roll_ms >= services_.audio.speech_segment.max_segment_ms) {
    throw std::runtime_error(
        "services.audio.speech_segment.pre_roll_ms must be less than max_segment_ms");
  }
  ValidateAddress(services_.camera.grpc.listen_address, "services.camera.grpc.listen_address");
  if (!IsOneOf(services_.camera.capture_backend, "gstreamer", "synthetic")) {
    throw std::runtime_error("services.camera.capture_backend must be gstreamer or synthetic");
  }
  RequirePositive(services_.camera.preview_stale_timeout_ms,
                  "services.camera.preview_stale_timeout_ms");
  if (services_.camera.synthetic_fault != "none" &&
      services_.camera.synthetic_fault != "no_frames" &&
      services_.camera.synthetic_fault != "stall" &&
      services_.camera.synthetic_fault != "disconnect") {
    throw std::runtime_error(
        "services.camera.synthetic_fault must be none, no_frames, stall, or disconnect");
  }
  if (services_.camera.synthetic_fault_after_frames < 0) {
    throw std::runtime_error("services.camera.synthetic_fault_after_frames must not be negative");
  }
  if (services_.camera.frame_transport != "shared_memory") {
    throw std::runtime_error("services.camera.frame_transport currently supports shared_memory");
  }
  RequireNotEmpty(services_.camera.shared_memory_name, "services.camera.shared_memory_name");
  if (services_.camera.shared_memory_name.front() != '/') {
    throw std::runtime_error("services.camera.shared_memory_name must begin with '/'");
  }
  RequirePositive(services_.camera.max_frame_bytes, "services.camera.max_frame_bytes");
  RequireNotEmpty(services_.camera.photo_directory, "services.camera.photo_directory");
  if (services_.camera.photo_jpeg_quality < 1 || services_.camera.photo_jpeg_quality > 100) {
    throw std::runtime_error("services.camera.photo_jpeg_quality must be between 1 and 100");
  }
  RequirePositive(services_.camera.photo_max_frame_age_ms,
                  "services.camera.photo_max_frame_age_ms");
  ValidateAddress(services_.voice_interaction.audio_address,
                  "services.voice_interaction.audio_address");
  ValidateAddress(services_.voice_interaction.gateway_address,
                  "services.voice_interaction.gateway_address");
  ValidateAddress(services_.voice_interaction.grpc.listen_address,
                  "services.voice_interaction.grpc.listen_address");
  RequirePositive(services_.voice_interaction.stream_timeout_ms,
                  "services.voice_interaction.stream_timeout_ms");
  RequirePositive(services_.voice_interaction.retry_delay_ms,
                  "services.voice_interaction.retry_delay_ms");
  RequireNotEmpty(services_.recording.directory, "services.recording.directory");
  ValidateAddress(services_.recording.vehicle_data_address,
                  "services.recording.vehicle_data_address");
  ValidateAddress(services_.recording.grpc.listen_address,
                  "services.recording.grpc.listen_address");
  RequirePositive(services_.recording.stream_timeout_ms, "services.recording.stream_timeout_ms");
  RequirePositive(services_.recording.retry_delay_ms, "services.recording.retry_delay_ms");
  RequirePositive(services_.recording.max_sessions, "services.recording.max_sessions");
  if (services_.recording.max_total_bytes == 0) {
    throw std::runtime_error("services.recording.max_total_bytes must be greater than zero");
  }

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
  if (hardware_.audio.sample_rate_hz != 16000) {
    throw std::runtime_error("hardware.audio.sample_rate_hz currently supports only 16000");
  }
  if (hardware_.audio.channels != 1) {
    throw std::runtime_error("hardware.audio.channels currently supports only mono (1)");
  }
  if (hardware_.audio.frame_ms != 20) {
    throw std::runtime_error("hardware.audio.frame_ms currently supports only 20");
  }
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
  if (features_.voice.asr_provider != "mock" && features_.voice.asr_provider != "whisper_cpp") {
    throw std::runtime_error("features.voice.asr_provider must be mock or whisper_cpp");
  }
  if (features_.voice.asr_provider == "whisper_cpp" && features_.voice.asr_model_path.empty()) {
    throw std::runtime_error("features.voice.asr_model_path is required for whisper_cpp");
  }
  if (features_.voice.asr_language.empty()) {
    throw std::runtime_error("features.voice.asr_language must not be empty");
  }
  if (features_.voice.asr_threads <= 0) {
    throw std::runtime_error("features.voice.asr_threads must be positive");
  }
  if (features_.voice.tts_provider != "mock") {
    throw std::runtime_error("features.voice.tts_provider currently supports only mock");
  }
  if (!IsOneOf(tools_.topic.backend, "file", "grpc")) {
    throw std::runtime_error("tools.topic.backend must be file or grpc");
  }
  RequireNotEmpty(tools_.topic.dir, "tools.topic.dir");

  for (std::size_t i = 0; i < runtime_.dependencies.size(); ++i) {
    const auto& dependency = runtime_.dependencies[i];
    const std::string prefix = "runtime.dependencies[" + std::to_string(i) + "]";
    RequireNotEmpty(dependency.service, prefix + ".service");
    for (const auto& required : dependency.required) {
      RequireNotEmpty(required, prefix + ".required");
      if (required == dependency.service) {
        throw std::runtime_error(prefix + ".required must not depend on itself");
      }
    }
    for (const auto& optional : dependency.optional) {
      RequireNotEmpty(optional, prefix + ".optional");
      if (optional == dependency.service) {
        throw std::runtime_error(prefix + ".optional must not depend on itself");
      }
    }
  }
}

}  // namespace config
}  // namespace cockpit
