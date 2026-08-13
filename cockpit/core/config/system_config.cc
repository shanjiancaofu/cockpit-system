#include "cockpit/core/config/system_config.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cockpit {
namespace config {
namespace {

void ValidateKeys(const YAML::Node& node, const std::string& path,
                  std::initializer_list<std::string_view> allowed_keys) {
  for (const auto& item : node) {
    if (!item.first.IsScalar()) {
      throw std::runtime_error(path.empty() ? "config keys must be scalars"
                                            : path + " keys must be scalars");
    }
    const std::string key = item.first.as<std::string>();
    if (std::find(allowed_keys.begin(), allowed_keys.end(), std::string_view(key)) ==
        allowed_keys.end()) {
      throw std::runtime_error((path.empty() ? key : path + "." + key) + " is not supported");
    }
  }
}

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
  constexpr std::string_view unix_prefix = "unix:";
  if (value.rfind(unix_prefix, 0) == 0) {
    const std::filesystem::path socket_path(value.substr(unix_prefix.size()));
    if (!socket_path.is_absolute() || socket_path.filename().empty()) {
      throw std::runtime_error(path + " Unix socket must use unix:/absolute/path format");
    }
    return;
  }
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
  ValidateKeys(root, "",
               {"system", "paths", "logging", "services", "hardware", "features", "tools"});

  SystemConfig config;
  const YAML::Node system = ChildMap(root, "system", "system");
  ValidateKeys(system, "system", {"name", "vehicle_id"});
  config.system_.name = Read(system, "name", config.system_.name, "system.name");
  config.system_.vehicle_id =
      Read(system, "vehicle_id", config.system_.vehicle_id, "system.vehicle_id");

  const YAML::Node paths = ChildMap(root, "paths", "paths");
  ValidateKeys(paths, "paths", {"data_dir", "log_dir", "run_dir"});
  config.paths_.data_dir = Read(paths, "data_dir", config.paths_.data_dir, "paths.data_dir");
  config.paths_.log_dir = Read(paths, "log_dir", config.paths_.log_dir, "paths.log_dir");
  config.paths_.run_dir = Read(paths, "run_dir", config.paths_.run_dir, "paths.run_dir");

  const YAML::Node logging = ChildMap(root, "logging", "logging");
  ValidateKeys(logging, "logging",
               {"level", "dump_time_secs", "cut_off_time_mins", "max_files", "mirror_stderr"});
  config.logging_.level = Read(logging, "level", config.logging_.level, "logging.level");
  config.logging_.dump_time_secs =
      Read(logging, "dump_time_secs", config.logging_.dump_time_secs, "logging.dump_time_secs");
  config.logging_.cut_off_time_mins = Read(
      logging, "cut_off_time_mins", config.logging_.cut_off_time_mins, "logging.cut_off_time_mins");
  config.logging_.max_files =
      Read(logging, "max_files", config.logging_.max_files, "logging.max_files");
  config.logging_.mirror_stderr =
      Read(logging, "mirror_stderr", config.logging_.mirror_stderr, "logging.mirror_stderr");

  const YAML::Node services = ChildMap(root, "services", "services");
  ValidateKeys(services, "services",
               {"vehicle_data", "gateway", "audio", "camera", "voice_interaction", "recording"});
  const YAML::Node vehicle_data = ChildMap(services, "vehicle_data", "services.vehicle_data");
  ValidateKeys(vehicle_data, "services.vehicle_data", {"source", "publish_interval_ms", "grpc"});
  config.services_.vehicle_data.source = Read(
      vehicle_data, "source", config.services_.vehicle_data.source, "services.vehicle_data.source");
  config.services_.vehicle_data.publish_interval_ms =
      Read(vehicle_data, "publish_interval_ms", config.services_.vehicle_data.publish_interval_ms,
           "services.vehicle_data.publish_interval_ms");
  const YAML::Node vehicle_grpc = ChildMap(vehicle_data, "grpc", "services.vehicle_data.grpc");
  ValidateKeys(vehicle_grpc, "services.vehicle_data.grpc", {"listen_address"});
  config.services_.vehicle_data.grpc.listen_address =
      Read(vehicle_grpc, "listen_address", config.services_.vehicle_data.grpc.listen_address,
           "services.vehicle_data.grpc.listen_address");

  const YAML::Node gateway = ChildMap(services, "gateway", "services.gateway");
  ValidateKeys(gateway, "services.gateway",
               {"vehicle_data_address", "stream_timeout_ms", "retry_delay_ms", "grpc"});
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
  ValidateKeys(gateway_grpc, "services.gateway.grpc", {"listen_address"});
  config.services_.gateway.grpc.listen_address =
      Read(gateway_grpc, "listen_address", config.services_.gateway.grpc.listen_address,
           "services.gateway.grpc.listen_address");
  const YAML::Node audio_service = ChildMap(services, "audio", "services.audio");
  ValidateKeys(audio_service, "services.audio", {"auto_start", "grpc"});
  config.services_.audio.auto_start = Read(
      audio_service, "auto_start", config.services_.audio.auto_start, "services.audio.auto_start");
  const YAML::Node audio_grpc = ChildMap(audio_service, "grpc", "services.audio.grpc");
  ValidateKeys(audio_grpc, "services.audio.grpc", {"listen_address"});
  config.services_.audio.grpc.listen_address =
      Read(audio_grpc, "listen_address", config.services_.audio.grpc.listen_address,
           "services.audio.grpc.listen_address");
  const YAML::Node camera_service = ChildMap(services, "camera", "services.camera");
  ValidateKeys(camera_service, "services.camera",
               {"grpc", "capture_backend", "preview_stale_timeout_ms", "synthetic_fault",
                "synthetic_fault_after_frames", "shared_memory_name", "max_frame_bytes",
                "photo_directory", "photo_jpeg_quality", "photo_max_frame_age_ms"});
  const YAML::Node camera_grpc = ChildMap(camera_service, "grpc", "services.camera.grpc");
  ValidateKeys(camera_grpc, "services.camera.grpc", {"listen_address"});
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
  ValidateKeys(voice_interaction, "services.voice_interaction",
               {"audio_address", "gateway_address", "stream_timeout_ms", "retry_delay_ms", "grpc"});
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
  ValidateKeys(voice_grpc, "services.voice_interaction.grpc", {"listen_address"});
  config.services_.voice_interaction.grpc.listen_address =
      Read(voice_grpc, "listen_address", config.services_.voice_interaction.grpc.listen_address,
           "services.voice_interaction.grpc.listen_address");

  const YAML::Node recording = ChildMap(services, "recording", "services.recording");
  ValidateKeys(recording, "services.recording",
               {"auto_start", "directory", "vehicle_data_address", "stream_timeout_ms",
                "retry_delay_ms", "max_sessions", "max_total_bytes", "max_session_bytes",
                "max_session_duration_seconds", "min_free_bytes", "grpc"});
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
  config.services_.recording.max_session_bytes =
      Read(recording, "max_session_bytes", config.services_.recording.max_session_bytes,
           "services.recording.max_session_bytes");
  config.services_.recording.max_session_duration_seconds =
      Read(recording, "max_session_duration_seconds",
           config.services_.recording.max_session_duration_seconds,
           "services.recording.max_session_duration_seconds");
  config.services_.recording.min_free_bytes =
      Read(recording, "min_free_bytes", config.services_.recording.min_free_bytes,
           "services.recording.min_free_bytes");
  const YAML::Node recording_grpc = ChildMap(recording, "grpc", "services.recording.grpc");
  ValidateKeys(recording_grpc, "services.recording.grpc", {"listen_address"});
  config.services_.recording.grpc.listen_address =
      Read(recording_grpc, "listen_address", config.services_.recording.grpc.listen_address,
           "services.recording.grpc.listen_address");

  const YAML::Node hardware = ChildMap(root, "hardware", "hardware");
  ValidateKeys(hardware, "hardware", {"can", "audio"});
  const YAML::Node can = ChildMap(hardware, "can", "hardware.can");
  ValidateKeys(can, "hardware.can",
               {"interface", "simulator_backend", "simulator_interval_ms", "receive_timeout_ms",
                "max_idle_timeouts"});
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
  ValidateKeys(audio, "hardware.audio",
               {"input_device", "output_device", "sample_rate_hz", "channels", "frame_ms"});
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
  ValidateKeys(features, "features", {"voice", "ai"});
  const YAML::Node voice = ChildMap(features, "voice", "features.voice");
  ValidateKeys(voice, "features.voice", {"enabled", "vad", "speech_segment", "asr"});
  config.features_.voice.enabled =
      Read(voice, "enabled", config.features_.voice.enabled, "features.voice.enabled");
  const YAML::Node vad = ChildMap(voice, "vad", "features.voice.vad");
  ValidateKeys(vad, "features.voice.vad", {"provider"});
  config.features_.voice.vad.provider =
      Read(vad, "provider", config.features_.voice.vad.provider, "features.voice.vad.provider");
  const YAML::Node speech_segment =
      ChildMap(voice, "speech_segment", "features.voice.speech_segment");
  ValidateKeys(speech_segment, "features.voice.speech_segment", {"pre_roll_ms", "max_segment_ms"});
  config.features_.voice.speech_segment.pre_roll_ms =
      Read(speech_segment, "pre_roll_ms", config.features_.voice.speech_segment.pre_roll_ms,
           "features.voice.speech_segment.pre_roll_ms");
  config.features_.voice.speech_segment.max_segment_ms =
      Read(speech_segment, "max_segment_ms", config.features_.voice.speech_segment.max_segment_ms,
           "features.voice.speech_segment.max_segment_ms");
  const YAML::Node asr = ChildMap(voice, "asr", "features.voice.asr");
  ValidateKeys(asr, "features.voice.asr", {"provider"});
  config.features_.voice.asr.provider =
      Read(asr, "provider", config.features_.voice.asr.provider, "features.voice.asr.provider");
  const YAML::Node ai = ChildMap(features, "ai", "features.ai");
  ValidateKeys(ai, "features.ai",
               {"asr_timeout_ms", "assistant_timeout_ms", "command_execution_timeout_ms",
                "tts_synthesis_timeout_ms", "follow_up_window_ms"});
  config.features_.ai.asr_timeout_ms =
      Read(ai, "asr_timeout_ms", config.features_.ai.asr_timeout_ms, "features.ai.asr_timeout_ms");
  config.features_.ai.assistant_timeout_ms =
      Read(ai, "assistant_timeout_ms", config.features_.ai.assistant_timeout_ms,
           "features.ai.assistant_timeout_ms");
  config.features_.ai.command_execution_timeout_ms =
      Read(ai, "command_execution_timeout_ms", config.features_.ai.command_execution_timeout_ms,
           "features.ai.command_execution_timeout_ms");
  config.features_.ai.tts_synthesis_timeout_ms =
      Read(ai, "tts_synthesis_timeout_ms", config.features_.ai.tts_synthesis_timeout_ms,
           "features.ai.tts_synthesis_timeout_ms");
  config.features_.ai.follow_up_window_ms =
      Read(ai, "follow_up_window_ms", config.features_.ai.follow_up_window_ms,
           "features.ai.follow_up_window_ms");

  const YAML::Node tools = ChildMap(root, "tools", "tools");
  ValidateKeys(tools, "tools", {"topic"});
  const YAML::Node topic = ChildMap(tools, "topic", "tools.topic");
  ValidateKeys(topic, "tools.topic", {"backend", "dir"});
  config.tools_.topic.backend =
      Read(topic, "backend", config.tools_.topic.backend, "tools.topic.backend");
  config.tools_.topic.dir = Read(topic, "dir", config.tools_.topic.dir, "tools.topic.dir");

  const char* runtime_dir = std::getenv("COCKPIT_RUNTIME_DIR");
  if (runtime_dir != nullptr && runtime_dir[0] != '\0') {
    const std::filesystem::path runtime_path(runtime_dir);
    config.paths_.data_dir = (runtime_path / "data").string();
    config.paths_.log_dir = (runtime_path / "logs").string();
    config.paths_.run_dir = (runtime_path / "run").string();
    config.tools_.topic.dir = (runtime_path / "logs/topics").string();
  }

  config.Validate();
  return config;
}

void SystemConfig::Validate() const {
  RequireNotEmpty(system_.name, "system.name");
  RequireNotEmpty(system_.vehicle_id, "system.vehicle_id");
  RequireNotEmpty(paths_.data_dir, "paths.data_dir");
  RequireNotEmpty(paths_.log_dir, "paths.log_dir");
  RequireNotEmpty(paths_.run_dir, "paths.run_dir");
  RequirePositive(logging_.dump_time_secs, "logging.dump_time_secs");
  RequirePositive(logging_.cut_off_time_mins, "logging.cut_off_time_mins");
  RequirePositive(logging_.max_files, "logging.max_files");
  if (logging_.dump_time_secs > 60) {
    throw std::runtime_error("logging.dump_time_secs must not exceed 60");
  }
  if (logging_.cut_off_time_mins > 1440) {
    throw std::runtime_error("logging.cut_off_time_mins must not exceed 1440");
  }
  if (logging_.max_files > 100) {
    throw std::runtime_error("logging.max_files must not exceed 100");
  }
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
  RequirePositive(services_.gateway.stream_timeout_ms, "services.gateway.stream_timeout_ms");
  RequirePositive(services_.gateway.retry_delay_ms, "services.gateway.retry_delay_ms");
  ValidateAddress(services_.audio.grpc.listen_address, "services.audio.grpc.listen_address");
  if (features_.voice.speech_segment.pre_roll_ms < 0) {
    throw std::runtime_error("features.voice.speech_segment.pre_roll_ms must not be negative");
  }
  RequirePositive(features_.voice.speech_segment.max_segment_ms,
                  "features.voice.speech_segment.max_segment_ms");
  if (features_.voice.speech_segment.pre_roll_ms > 2000) {
    throw std::runtime_error("features.voice.speech_segment.pre_roll_ms must not exceed 2000");
  }
  if (features_.voice.speech_segment.max_segment_ms > 60000) {
    throw std::runtime_error("features.voice.speech_segment.max_segment_ms must not exceed 60000");
  }
  if (features_.voice.speech_segment.pre_roll_ms >= features_.voice.speech_segment.max_segment_ms) {
    throw std::runtime_error(
        "features.voice.speech_segment.pre_roll_ms must be less than max_segment_ms");
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
  RequireNotEmpty(services_.camera.shared_memory_name, "services.camera.shared_memory_name");
  if (services_.camera.shared_memory_name.front() != '/' ||
      services_.camera.shared_memory_name.size() == 1 ||
      services_.camera.shared_memory_name.find('/', 1) != std::string::npos ||
      services_.camera.shared_memory_name.size() > 255) {
    throw std::runtime_error(
        "services.camera.shared_memory_name must be one POSIX shared-memory name");
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
  if (services_.recording.max_session_bytes == 0 ||
      services_.recording.max_session_bytes > services_.recording.max_total_bytes) {
    throw std::runtime_error(
        "services.recording.max_session_bytes must be positive and not exceed max_total_bytes");
  }
  RequirePositive(services_.recording.max_session_duration_seconds,
                  "services.recording.max_session_duration_seconds");
  if (services_.recording.min_free_bytes == 0) {
    throw std::runtime_error("services.recording.min_free_bytes must be greater than zero");
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
  if (features_.voice.speech_segment.pre_roll_ms % hardware_.audio.frame_ms != 0) {
    throw std::runtime_error(
        "features.voice.speech_segment.pre_roll_ms must align with hardware.audio.frame_ms");
  }
  if (features_.voice.speech_segment.max_segment_ms % hardware_.audio.frame_ms != 0) {
    throw std::runtime_error(
        "features.voice.speech_segment.max_segment_ms must align with hardware.audio.frame_ms");
  }
  RequireNotEmpty(hardware_.audio.input_device, "hardware.audio.input_device");
  RequireNotEmpty(hardware_.audio.output_device, "hardware.audio.output_device");
  RequirePositive(features_.ai.asr_timeout_ms, "features.ai.asr_timeout_ms");
  RequirePositive(features_.ai.assistant_timeout_ms, "features.ai.assistant_timeout_ms");
  RequirePositive(features_.ai.command_execution_timeout_ms,
                  "features.ai.command_execution_timeout_ms");
  RequirePositive(features_.ai.tts_synthesis_timeout_ms, "features.ai.tts_synthesis_timeout_ms");
  RequirePositive(features_.ai.follow_up_window_ms, "features.ai.follow_up_window_ms");
  if (!IsOneOf(features_.voice.vad.provider, "disabled", "mock")) {
    throw std::runtime_error("features.voice.vad.provider must be disabled or mock");
  }
  if (features_.voice.asr.provider != "mock") {
    throw std::runtime_error("features.voice.asr.provider must be mock");
  }
  if (features_.voice.enabled && features_.voice.vad.provider == "disabled") {
    throw std::runtime_error(
        "features.voice.vad.provider must be mock when features.voice.enabled is true");
  }
  if (features_.voice.enabled && !services_.audio.auto_start) {
    throw std::runtime_error(
        "services.audio.auto_start must be true when features.voice.enabled is true");
  }
  if (!IsOneOf(tools_.topic.backend, "file", "grpc")) {
    throw std::runtime_error("tools.topic.backend must be file or grpc");
  }
  RequireNotEmpty(tools_.topic.dir, "tools.topic.dir");
}

}  // namespace config
}  // namespace cockpit
