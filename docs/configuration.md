# 配置说明

主配置文件是 `configs/config.yaml`，由 `core/config/SystemConfig` 解析并在进程启动时校验。
所有服务共享同一份配置结构，但只读取自己需要的 section。

## 基础信息

```yaml
system:
  name: cockpit-system
  vehicle_id: car_001

paths:
  data_dir: data
  log_dir: logs
```

- `system.name`：系统名称。
- `system.vehicle_id`：车辆标识，未来用于云端 topic、存储和诊断。
- `paths.data_dir`：运行数据目录。
- `paths.log_dir`：日志目录。

## 日志

```yaml
logging:
  level: info
  max_bytes: 2097152
  mirror_stderr: true
```

- `level`：`debug/info/warn/error`。
- `max_bytes`：单个日志文件的大小限制。
- `mirror_stderr`：是否同时输出到终端。

## 服务地址

| 服务 | 默认地址 | 用途 |
|---|---|---|
| vehicle-data | `127.0.0.1:50050` | VehicleState streaming |
| gateway | `127.0.0.1:50051` | UI、topic 和车辆状态查询 |
| audio | `127.0.0.1:50052` | 音频控制、transcript、Speak |
| voice interaction | `127.0.0.1:50053` | 语音交互控制和回复 |
| camera | `127.0.0.1:50054` | 相机 list/start/stop/status |

`retry_delay_ms` 控制断线重试基础间隔，`stream_timeout_ms` 控制 streaming RPC 的会话超时。

## 车辆数据

```yaml
services:
  vehicle_data:
    source: mock
    publish_interval_ms: 200

hardware:
  can:
    interface: vcan0
    simulator_backend: stdout
    simulator_interval_ms: 100
    receive_timeout_ms: 500
    max_idle_timeouts: 10
```

- `source`：`mock` 或 `socketcan`。
- `interface`：SocketCAN 接口，例如 `vcan0`、`can0`。
- `simulator_backend`：`stdout` 或 `socketcan`。

## 音频

```yaml
hardware:
  audio:
    capture_backend: alsa
    playback_backend: alsa
    input_device: default
    output_device: default
    sample_rate_hz: 16000
    channels: 1
    frame_ms: 20
```

当前语音链路固定使用 16 kHz、mono、PCM16、20 ms frame。Jetson 部署时将 `input_device` 和
`output_device` 改为实际 ALSA device，例如 `plughw:1,0`。

VAD：

```yaml
services:
  audio:
    auto_start: false
    vad:
      enabled: true
      backend: energy
      speech_threshold_dbfs: -40.0
      speech_start_frames: 3
      speech_end_frames: 10
    speech_segment:
      pre_roll_ms: 100
      max_segment_ms: 15000
```

- `speech_threshold_dbfs` 必须根据真实麦克风环境标定。
- `speech_start_frames` 防止短噪声触发。
- `speech_end_frames` 提供尾部静音 hangover。
- `pre_roll_ms` 保留起始语音前的少量音频。

## 相机

```yaml
services:
  camera:
    frame_transport: shared_memory
    shared_memory_name: /cockpit_camera_preview
    max_frame_bytes: 8388608
```

帧像素通过 POSIX shared memory 传输，gRPC 只负责控制和状态。`max_frame_bytes` 必须覆盖目标
分辨率和像素格式的单帧大小。

## 语音和 AI

```yaml
features:
  voice:
    enabled: false
    mode: push_to_talk
    asr_provider: mock
    asr_model_path: ""
    asr_language: zh
    asr_threads: 4
    tts_provider: mock
  ai:
    provider: mock
    model: local-demo
    request_timeout_ms: 10000
```

启用 whisper.cpp：

```yaml
features:
  voice:
    enabled: true
    asr_provider: whisper_cpp
    asr_model_path: /cockpit-system/models/whisper/ggml-small.bin
    asr_language: zh
```

当前 `tts_provider` 和 `features.ai` 仍是 mock。未知 provider 会在启动校验或创建 provider 时失败，
不会静默回退。

## 配置原则

- 配置文件描述部署差异，不存放业务状态。
- 密钥和 token 不提交到仓库，未来使用环境变量或独立 secret 文件。
- 新增配置必须同步类型化结构、校验、示例和测试。
- 不允许模块自行重复解析 YAML。
