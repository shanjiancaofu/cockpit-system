#pragma once

#include <string>
#include <vector>

namespace cockpit {
namespace config {

struct SystemIdentityConfig {
  std::string name = "cockpit-system";
  std::string vehicle_id = "car_001";
};

struct PathsConfig {
  std::string data_dir = "data";
  std::string log_dir = "logs";
};

struct LoggingConfig {
  std::string level = "info";
  int max_bytes = 2 * 1024 * 1024;
  bool mirror_stderr = true;
};

struct GrpcServerConfig {
  std::string listen_address;
};

struct WebSocketServerConfig {
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
  WebSocketServerConfig websocket{"127.0.0.1:18080"};
};

struct VadConfig {
  bool enabled = true;
  std::string backend = "energy";
  double speech_threshold_dbfs = -40.0;
  int speech_start_frames = 3;
  int speech_end_frames = 10;
};

struct SpeechSegmentConfig {
  int pre_roll_ms = 100;
  int max_segment_ms = 15000;
};

struct AudioServiceConfig {
  bool auto_start = false;
  GrpcServerConfig grpc{"127.0.0.1:50052"};
  VadConfig vad;
  SpeechSegmentConfig speech_segment;
};

struct CameraServiceConfig {
  GrpcServerConfig grpc{"127.0.0.1:50054"};
  std::string frame_transport = "shared_memory";
  std::string shared_memory_name = "/cockpit_camera_preview";
  int max_frame_bytes = 8 * 1024 * 1024;
  std::string photo_directory = "photos";
  int photo_jpeg_quality = 90;
  int photo_max_frame_age_ms = 2000;
};

struct VoiceInteractionServiceConfig {
  std::string audio_address = "127.0.0.1:50052";
  std::string gateway_address = "127.0.0.1:50051";
  int stream_timeout_ms = 10000;
  int retry_delay_ms = 200;
  GrpcServerConfig grpc{"127.0.0.1:50053"};
};

struct MqttConfig {
  std::string broker = "tcp://127.0.0.1:1883";
  std::string telemetry_topic = "vehicle/status";
  int qos = 1;
};

struct CloudUplinkConfig {
  bool enabled = false;
  MqttConfig mqtt;
};

struct RecordingServiceConfig {
  bool auto_start = false;
  std::string directory = "recordings";
  std::string vehicle_data_address = "127.0.0.1:50050";
  int stream_timeout_ms = 10000;
  int retry_delay_ms = 200;
  int max_sessions = 100;
  std::uint64_t max_total_bytes = 5368709120ULL;
  GrpcServerConfig grpc{"127.0.0.1:50055"};
};

struct ServicesConfig {
  VehicleDataConfig vehicle_data;
  GatewayConfig gateway;
  AudioServiceConfig audio;
  CameraServiceConfig camera;
  VoiceInteractionServiceConfig voice_interaction;
  CloudUplinkConfig cloud_uplink;
  RecordingServiceConfig recording;
};

struct CanConfig {
  std::string interface = "vcan0";
  std::string simulator_backend = "stdout";
  int simulator_interval_ms = 100;
  int receive_timeout_ms = 500;
  int max_idle_timeouts = 10;
};

struct AudioConfig {
  std::string capture_backend = "alsa";
  std::string playback_backend = "alsa";
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

struct VoiceConfig {
  bool enabled = false;
  std::string mode = "push_to_talk";
  std::string asr_provider = "mock";
  std::string asr_model_path;
  std::string asr_language = "zh";
  int asr_threads = 4;
  std::string tts_provider = "mock";
};

struct AiConfig {
  std::string provider = "mock";
  std::string model = "local-demo";
  int request_timeout_ms = 10000;
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

struct ServiceDependencyConfig {
  std::string service;
  std::vector<std::string> required;
  std::vector<std::string> optional;
};

struct RuntimeConfig {
  std::vector<ServiceDependencyConfig> dependencies = {
      {"cockpit-gateway-service", {"vehicle-data-service"}, {}},
      {"voice-interaction-service",
       {"audio-service", "cockpit-gateway-service"},
       {"recording-service"}},
      {"camera-service", {}, {"recording-service"}},
      {"recording-service", {"vehicle-data-service"}, {}},
      {"cockpit-ui",
       {"cockpit-gateway-service", "camera-service"},
       {"audio-service", "voice-interaction-service", "recording-service"}},
  };
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
  const RuntimeConfig& runtime() const {
    return runtime_;
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
  RuntimeConfig runtime_;
};

}  // namespace config
}  // namespace cockpit
