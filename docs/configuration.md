# 配置说明

主配置文件是 `configs/config.yaml`，由 `cockpit/core/config/SystemConfig` 解析并在进程启动时校验。
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
| recording | `127.0.0.1:50055` | 研发录包 start/stop/status |

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
    photo_directory: photos
    photo_jpeg_quality: 90
    photo_max_frame_age_ms: 2000
```

帧像素通过 POSIX shared memory 传输，gRPC 只负责控制和状态。`max_frame_bytes` 必须覆盖目标
分辨率和像素格式的单帧大小。

- 相对 `photo_directory` 以 `paths.data_dir` 为基准，默认保存到 `data/photos`。
- `photo_jpeg_quality` 范围为 1-100。
- 拍照只接受不超过 `photo_max_frame_age_ms` 的最新帧，避免保存卡住的旧画面。
- camera-service 内部通过 MessageBus 发布 `/camera/status` 和 `/camera/frame_meta`，录包桥只转发
  轻量元数据，不传输帧像素。

## 研发录包

```yaml
services:
  recording:
    auto_start: false
    directory: recordings
    vehicle_data_address: 127.0.0.1:50050
    stream_timeout_ms: 10000
    retry_delay_ms: 200
    max_sessions: 100
    max_total_bytes: 5368709120
    grpc:
      listen_address: 127.0.0.1:50055
```

- 相对 `directory` 以 `paths.data_dir` 为基准，默认写入 `data/recordings/sessions`。
- `auto_start` 适合固定诊断任务；日常开发建议通过 `recording-ctl` 显式控制。
- 完成会话包含 `manifest.json`、`vehicle_state.jsonl`、`events.jsonl` 和 `COMPLETE`。
- `manifest.json` 记录 project、schema_version、vehicle_id、config_path 和 sources，便于研发复盘。
- `events.jsonl` 只保存轻量研发事件元数据；相机图片、视频和音频数据应保存为独立文件，事件中只记录路径、句柄和时间戳。
- 异常退出遗留的 `.recording_*` 会在下次启动时改名为 `interrupted_*` 并写入
  `INTERRUPTED` 标记。
- 当前已记录 VehicleState，并通过 `AppendEvent` 接收通用事件；camera 拍照结果和 voice response
  可写入 `events.jsonl`；camera status/frame metadata 通过 camera-service 内部 MessageBus 桥接到
  recording-service。相机、音频大块数据源尚未接入。
- `max_sessions` 和 `max_total_bytes` 同时生效；完成会话后自动从最旧数据开始清理。
- `recording-ctl --prune` 按同一策略立即执行清理，不会删除当前活动会话。

## Runtime 依赖

```yaml
runtime:
  dependencies:
    - service: voice-interaction-service
      required: [audio-service, cockpit-gateway-service]
      optional: [recording-service]
```

- `required`：核心依赖，部署时应保证先启动或可用。
- `optional`：弱依赖，不可用时服务主流程继续运行，但会降级或只记录 warning。
- 查看当前依赖图：`cockpit-ctl dependencies --config configs/config.yaml`。

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
