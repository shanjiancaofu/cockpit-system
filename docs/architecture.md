# 当前架构概览

本文只描述当前代码已经形成的真实运行架构。长期目标和未落地设计见
[architecture_refined_v0.4.md](architecture_refined_v0.4.md)，模块完成度见 [实现状态.md](实现状态.md)。

## 项目定位

`cockpit-system` 是运行在 Jetson/Linux 上的智能座舱车端系统。当前保持单仓库，通过 CMake
target 和职责目录实现内部模块化，不提前拆分云端前端、后端或共享协议仓库。

项目采用 C++17、CMake、Ninja、protobuf 和 gRPC。`znavigator` 主要作为运行时组织、薄入口、
独立 target 和模块边界的参考，不照搬动态插件、复杂发布规则和历史兼容结构。

## 分层结构

```text
tools ───────────────┐
                     ↓
cockpit/apps → cockpit/services    长运行进程、设备所有权、对外控制接口
                         ↓
                   cockpit/modules 平台无关领域模型与处理流程
                         ↓
                   cockpit/drivers Linux/硬件适配
                         ↓
                     cockpit/core  通用基础设施
```

主要目录：

```text
cockpit/
├── apps/cockpit-ui/       Qt 6/QML 车机界面
├── core/                  通用基础设施
├── drivers/               Linux/硬件适配
├── modules/               audio、camera、recording、vehicle、voice
├── proto/                 protobuf/gRPC 契约
└── services/              车端守护进程
tools/                     诊断和模拟工具
tests/                     单元测试与 smoke test
```

## 进程职责

- `vehicle-data-service`：独占 CAN 或 mock 车辆数据源，发布 `VehicleState`。
- `cockpit-gateway-service`：聚合车辆状态，向 UI、topic 和语音动作提供数据。
- `audio-service`：独占麦克风和扬声器，运行采集、VAD、分段、ASR 和 TTS 播放。
- `camera-service`：独占摄像头，负责预览生命周期和共享内存写入。
- `voice-interaction-service`：订阅识别文本，执行意图、动作和语音回复编排。
- `recording-service`：面向研发诊断，订阅车辆状态并管理持久化录包会话。
- `cloud-uplink-service`：当前为 MQTT 上传占位实现。

## 通信模型

```text
同线程             函数调用
同进程控制         callback / EventQueue
音频连续流         SPSC RingBuffer
相机跨进程帧       POSIX Shared Memory 双缓冲
控制、状态、文本   gRPC unary / streaming
外部云端           MQTT / WebSocket（后续）
```

gRPC 不承载 PCM、图片等高频大数据。控制面和数据面分离：gRPC 管理生命周期、状态和文本；
ring buffer 或共享内存传输连续数据。

## 车辆链路

```text
can-simulator / SocketCAN
    → vehicle-data-service
    → VehicleState gRPC stream
    → cockpit-gateway-service
    → cockpit-ui / topic / voice action
```

当前 CAN 映射是原型格式；正式车辆接入必须基于确认后的 DBC 或信号文档。

## 音频与语音链路

```text
ALSA microphone
    → AudioCaptureStream
    → AudioFrame（20 ms）
    → SPSC RingBuffer
    → Energy VAD
    → SpeechSegmenter
    → mock ASR / whisper.cpp
    → voice-interaction-service
    → intent / action
    → mock TTS
    → ALSA speaker
```

PCM 和语音片段保持在 `audio-service` 进程内，只有 transcript、控制和指标通过 gRPC。

## 相机链路

```text
USB Camera
    → V4L2
    → GStreamer appsink
    → camera-service
    → POSIX Shared Memory 双缓冲
    → Qt camera worker
    → QML Camera 页面
```

UI 能区分等待首帧、实时画面、卡帧、最后一帧和共享内存断开，并在 writer 重启后自动重连。
共享帧槽使用 robust process-shared mutex；writer 异常退出后，新实例会回收遗留的 POSIX shared
memory，reader 会拒绝布局、stride 或 payload 长度不一致的帧。
拍照请求通过 gRPC 到 camera-service，服务读取共享内存最新帧并用 GStreamer 编码 JPEG；
camera-ctl 和 Qt UI 都不直接访问摄像头设备。

## 研发录包链路

```text
vehicle-data-service
    → VehicleState gRPC stream
    → recording-service
    → sessions/.recording_<id>/vehicle_state.jsonl
camera/voice/audio metadata
    → asynchronous recording publisher
    → recording-service event/data-file writer
    → sessions/.recording_<id>/events.jsonl + data_files.jsonl + artifacts/
    → sessions/<id>/manifest.json + COMPLETE
```

`recording-ctl` 通过 gRPC 启动、停止、查询、删除和清理会话。原始数据以文件为权威来源；
进程异常退出后，下次启动将未完成目录标记为 `interrupted_*`。目录索引从 manifest 重建，
并按最大会话数和总字节数清理最旧数据。`events.jsonl` 只保存轻量研发事件元数据，大块图片、
音频和视频仍以独立文件保存；需要纳入会话保留策略的文件会复制到 `artifacts/`，再写入相对路径
索引。camera 和 voice 使用有界后台队列投递录包数据，录包服务不可用不会阻塞用户主流程。该服务
属于研发诊断边界，不接收用户语音动作。

### 录包时间语义

recording-service 已提供多源时间线查询：读取 `vehicle_state.jsonl`、`events.jsonl` 和
`data_files.jsonl`，按主机侧 `timestamp_ms` 稳定排序，并支持时间范围和条数限制。损坏的 JSONL
行会被跳过并计数，单行损坏不会阻断整段研发复盘。它不负责同步或校准 ECU、相机、音频设备等
独立时钟，也不估算时钟偏移和漂移。

当前单机原型默认各服务使用同一 Jetson/WSL 主机时钟，时间线条目保留 `timestamp_ms`、`source`、
`kind`、label、原始 JSON 和可选 path。真实硬件提供 ECU timestamp、camera PTS 或 audio sample clock
后，再按需要增加 `source_timestamp`、`host_timestamp`、`monotonic_timestamp`、`clock_domain`、
`offset` 和 `uncertainty`，单独实现时间戳归一化；当前不提前引入 PTP 或漂移估计。

## 当前边界

已具备可运行的 WSL/Jetson 车机原型架构，但尚缺正式 DBC、真实 TTS、麦克风/扬声器标定、
Jetson CUDA/TensorRT 验证、音视频多源录包、MQTT、WebSocket、视觉 AI 和完整 LLM
应用层。
