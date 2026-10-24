#pragma once

#include <string>

namespace cockpit {
namespace config {

struct SystemIdentityConfig {
  std::string name = "cockpit-system";
  std::string vehicle_id = "car_001";
};

struct PathsConfig {
  std::string data_dir = "data";
  std::string log_dir = "logs";
  std::string run_dir = "run";
};

struct LoggingConfig {
  std::string level = "info";
  int dump_time_secs = 5;
  int cut_off_time_mins = 5;
  int max_files = 10;
  bool mirror_stderr = true;
};

struct GrpcServerConfig {
  std::string listen_address;
};

struct VehicleDataConfig {
  std::string source = "mock";
  int publish_interval_ms = 200;
  GrpcServerConfig grpc{"127.0.0.1:50050"};
};

struct GatewayConfig {
  std::string vehicle_data_address = "127.0.0.1:50050";
  int stream_timeout_ms = 10000;
  int retry_delay_ms = 200;
  GrpcServerConfig grpc{"127.0.0.1:50051"};
};

struct VadConfig {
  std::string provider = "mock";
};

struct KwsConfig {
  bool enabled = false;
  std::string provider = "mock";
  int cooldown_ms = 1500;
  std::string wake_word = "你好小山";
  std::string keywords_file;
  std::string model_dir;
};

struct SpeechSegmentConfig {
  int pre_roll_ms = 100;
  int max_segment_ms = 15000;
};

struct AudioDriverConfig {
  bool auto_start = false;
  GrpcServerConfig grpc{"127.0.0.1:50052"};
};

struct CameraServiceConfig {
  GrpcServerConfig grpc{"127.0.0.1:50054"};
  std::string capture_pipeline = "argus_isp";
  std::string uvc_input_format = "mjpeg";
  int preview_stale_timeout_ms = 2000;
  std::string synthetic_fault = "none";
  int synthetic_fault_after_frames = 30;
  std::string shared_memory_name = "/cockpit_camera_preview";
  int max_frame_bytes = 8 * 1024 * 1024;
  std::string photo_directory = "photos";
  int photo_jpeg_quality = 90;
  int photo_max_frame_age_ms = 2000;
  std::string calibration_file;
  std::string calibration_pipeline;
  std::string calibration_device;
};

struct VoiceInteractionServiceConfig {
  std::string audio_address = "127.0.0.1:50052";
  std::string gateway_address = "127.0.0.1:50051";
  int stream_timeout_ms = 10000;
  int retry_delay_ms = 200;
  GrpcServerConfig grpc{"127.0.0.1:50053"};
};

struct MediaServiceConfig {
  std::string provider = "disabled";
  std::string manifest = "media/manifest.yaml";
  std::string sink = "fakesink";
  GrpcServerConfig grpc{"127.0.0.1:50056"};
};

struct RecordingServiceConfig {
  bool auto_start = false;
  std::string directory = "recordings";
  std::string vehicle_data_address = "127.0.0.1:50050";
  int stream_timeout_ms = 10000;
  int retry_delay_ms = 200;
  int max_sessions = 100;
  std::uint64_t max_total_bytes = 5368709120ULL;
  std::uint64_t max_session_bytes = 1073741824ULL;
  int max_session_duration_seconds = 14400;
  std::uint64_t min_free_bytes = 536870912ULL;
  GrpcServerConfig grpc{"127.0.0.1:50055"};
};

struct SentinelServiceConfig {
  bool auto_arm = false;
  std::string vehicle_data_address = "127.0.0.1:50050";
  int cooldown_ms = 30000;
  int max_event_age_ms = 5000;
  int queue_capacity = 64;
  int rpc_timeout_ms = 1000;
  GrpcServerConfig grpc{"127.0.0.1:50057"};
};

struct BridgeServiceConfig {
  std::string provider = "disabled";
  std::string fake_outcome = "succeeded";
  std::string nav2_action_name = "/navigate_to_pose";
  int nav2_server_timeout_ms = 1000;
  int goal_timeout_ms = 30000;
  GrpcServerConfig grpc{"127.0.0.1:50058"};
};

struct ServicesConfig {
  VehicleDataConfig vehicle_data;
  GatewayConfig gateway;
  AudioDriverConfig audio;
  CameraServiceConfig camera;
  VoiceInteractionServiceConfig voice_interaction;
  MediaServiceConfig media;
  RecordingServiceConfig recording;
  SentinelServiceConfig sentinel;
  BridgeServiceConfig bridge;
};

struct CanConfig {
  std::string interface = "vcan0";
  std::string simulator_backend = "stdout";
  int simulator_interval_ms = 100;
  int receive_timeout_ms = 500;
  int max_idle_timeouts = 10;
};

struct AudioConfig {
  std::string input_device = "default";
  std::string output_device = "default";
  int sample_rate_hz = 16000;
  int channels = 1;
  int frame_ms = 20;
};

struct HardwareConfig {
  CanConfig can;
  AudioConfig audio;
};

struct AsrConfig {
  std::string provider = "mock";
};

struct TtsConfig {
  std::string provider = "mock";
  int speaker_id = 3;
  double speed = 1.0;
};

struct VoiceConfig {
  bool enabled = false;
  KwsConfig kws;
  VadConfig vad;
  SpeechSegmentConfig speech_segment;
  AsrConfig asr;
  TtsConfig tts;
};

struct LocalLlmConfig {
  bool enabled = false;
  std::string provider = "disabled";
  bool manage_process = false;
  std::string executable;
  std::string model_path;
  std::string host = "127.0.0.1";
  int port = 8080;
  std::string path = "/v1/chat/completions";
  std::string model = "Qwen3.5-2B";
  std::string system_prompt = "You are a cockpit assistant.";
  int max_tokens = 128;
  double temperature = 0.2;
  int first_token_timeout_ms = 5000;
  int response_timeout_ms = 30000;
  int context_size = 2048;
  int gpu_layers = 0;
  int startup_timeout_ms = 60000;
};

struct AiConfig {
  int asr_timeout_ms = 3000;
  int assistant_timeout_ms = 10000;
  int command_execution_timeout_ms = 3000;
  int tts_synthesis_timeout_ms = 5000;
  int follow_up_window_ms = 8000;
  LocalLlmConfig local_llm;
};

struct FeaturesConfig {
  VoiceConfig voice;
  AiConfig ai;
};

struct TopicToolConfig {
  std::string backend = "file";
  std::string dir = "logs/topics";
};

struct ToolsConfig {
  TopicToolConfig topic;
};

class SystemConfig {
 public:
  static SystemConfig LoadFromFile(const std::string& path);

  const SystemIdentityConfig& system() const {
    return system_;
  }
  const PathsConfig& paths() const {
    return paths_;
  }
  const LoggingConfig& logging() const {
    return logging_;
  }
  const ServicesConfig& services() const {
    return services_;
  }
  const HardwareConfig& hardware() const {
    return hardware_;
  }
  const FeaturesConfig& features() const {
    return features_;
  }
  const ToolsConfig& tools() const {
    return tools_;
  }

 private:
  void Validate() const;

  SystemIdentityConfig system_;
  PathsConfig paths_;
  LoggingConfig logging_;
  ServicesConfig services_;
  HardwareConfig hardware_;
  FeaturesConfig features_;
  ToolsConfig tools_;
};

}  // namespace config
}  // namespace cockpit
