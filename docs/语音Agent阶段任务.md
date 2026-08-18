# Cockpit Agent 与语音系统阶段任务表

更新时间：2026-08-17。

本文是语音专项的唯一任务表。稳定设计以
[语音Agent架构](语音Agent架构.md) 为准；后续对话先读下面的交接信息，再按阶段标题定位，
不需要读取全文。

## 对话阅读入口

| 范围 | 文档 | 状态 |
| --- | --- | --- |
| 阶段 0–5 | 分层、Audio Driver、PCM、Agent 基础 | 已完成工程迁移；真实声学验收延后 |
| 阶段 6–10 | 会话、KWS、Sherpa、ASR、命令路由 | 阶段 6、阶段 10 第一批已封板；阶段 7 KWS 主链路已起步；整体进行中 |
| 阶段 11–15 | LLM、TTS、前处理、模型发布 | 阶段 11 进行中，其余未开始 |
| 实施顺序和优先级 | 文末 | 持续更新 |

## 当前交接信息

```text
当前开发主线：阶段 11 本地 LLM；阶段 7–9 的 Jetson 声学与性能验收并行待办
已完成：分环节 deadline、固定错误提示、request-scoped Action 取消、single-flight playback 取消、
        ASR Stop stale-success 隔离、真实播放回执/取消和 FOLLOW_UP 窗口；TranscriptNormalizer、
        现有三种 Action 的显式正向整句 allowlist；所有非白名单 transcript fail closed；
        WakeWordDetector 接口、VoiceInputGate、cooldown、异步固定 PCM wake prompt、自定义唤醒词配置；
        Sherpa KWS/Silero VAD/SenseVoice provider 代码；LocalLlmClient、llama-server SSE、首 token/
        总超时，以及 Agent 托管 server 的 readiness、health、restart 和进程组回收
下一主线：固定审查过的 llama.cpp commit 与 Qwen3.5 GGUF 哈希，完成 Ubuntu VM 真实模型 smoke；
          Jetson 继续完成 Sherpa/音频硬件声学和性能验收
当前实现入口：agent/llm/, agent/runtime/agent_runtime.cc, agent/speech/providers/sherpa/,
             cockpit/modules/voice/assistant/
验证基线：Debug/Release、ASan/UBSan、TSan 和 driver dependency boundary 由 CI 持续验证
```

更新任务时，只更改对应分册的勾选项和本节交接信息；不把架构正文复制到任务文档。

## 1. 目标

本任务用于完成 `cockpit-system` 的音频驱动层、Agent 层和语音模型层重构。

核心目标：

- `cockpit/drivers/` 只负责硬件适配。
- `cockpit/modules/` 只保留与硬件和具体模型无关的接口、数据结构和基础能力。
- `cockpit/library/driver/` 只负责 Navigator 中的驱动模块组装、运行和传输。
- 顶层 `agent/` 负责 KWS、VAD、切句、ASR、LLM、TTS 和会话状态。
- `cockpit/navigator/library/agent/` 只保留 Navigator 的薄入口。
- Sherpa-ONNX、ONNX Runtime 和模型不进入主项目 CMake。
- 车控命令优先走确定性规则，LLM 只生成开放问答回复文本。

---

## 2. 最终目录目标

```text
cockpit-system/
├── cockpit/
│   ├── core/
│   ├── drivers/
│   │   ├── alsa/
│   │   ├── socketcan/
│   │   └── v4l2/
│   ├── modules/
│   │   ├── audio/
│   │   └── voice/
│   ├── library/
│   │   └── driver/
│   ├── navigator/
│   │   └── library/agent/   # Navigator 薄 ABI 入口
│   └── proto/
│
├── agent/
│   ├── runtime/
│   ├── speech/
│   │   ├── kws/
│   │   ├── vad/
│   │   ├── segment/
│   │   ├── asr/
│   │   └── tts/
│   ├── interaction/
│   ├── conversation/
│   ├── llm/
│   ├── actions/
│   ├── configs/
│   └── tests/
│
├── configs/
├── scripts/
└── tests/
```

---

## 3. 分层约束

### `cockpit/drivers/`

只允许：

- ALSA、V4L2、SocketCAN 等硬件接口。
- `open/close/read/write/ioctl`。
- ALSA XRun 恢复。
- 设备状态读取。
- 硬件错误转换。

禁止：

- KWS、VAD、ASR、TTS。
- SpeechSegmenter。
- Agent、LLM、Conversation。
- Sherpa-ONNX、ONNX Runtime。
- 业务语义和 Transcript。

### `cockpit/modules/`

只保留：

- 音频帧和 PCM 格式。
- 采集、播放抽象接口。
- WAV、音量检测等基础能力。
- 音频传输协议与语音互作需要的稳定领域数据。

禁止：

- ALSA、V4L2 等具体硬件实现。
- Silero、SenseVoice、Qwen 等具体模型实现。
- Navigator 运行组装。
- Agent 应用逻辑。
- 为每个 VAD、ASR 或 TTS 实现增加运行时插件 ABI。

### `cockpit/library/driver/`

负责：

- 驱动模块入口。
- 设备生命周期。
- PCM 采集与播放。
- PCM 传输。
- gRPC Server 或共享内存端点。
- 驱动状态和错误上报。

禁止：

- VAD、ASR、TTS。
- Transcript。
- LLM、Agent 和会话逻辑。

自有类避免使用 `Service` 命名，建议：

```text
audio_runtime
audio_capture
audio_playback
audio_stream_server
audio_grpc_server
camera_controller
vehicle_data_provider
```

### 顶层 `agent/`

负责：

- KWS、VAD、SpeechSegmenter。
- ASR、TTS。
- 会话状态机。
- Transcript 规范化。
- 确定性命令匹配。
- ActionDispatcher。
- llama.cpp 客户端。
- 开放问答和回复播放。
- 超时、打断和异常恢复。

### `cockpit/navigator/library/agent/`

只保留薄入口：

```text
cockpit/navigator/library/agent/
└── agent_entry.cc
```

---

## 阶段 0–5：分层与 Agent 基础

状态：工程迁移已完成。真实麦克风、真实 VAD 和声学指标属于后续 Jetson 验收，不因 mock
链路通过而标记完成。

> 本节保留最初任务明细用于追踪，但其中曾经出现的“driver 依赖 modules”方案已经废弃。
> 下面以当前最终依赖方向为准；`[x]` 表示代码边界或自动化验证已完成，`[ ]` 表示仍需真实
> provider 或硬件证据。

### 阶段 0：现状冻结与依赖审计

#### 目标

在修改前确认当前目录、目标依赖和混层位置，避免重构过程中丢失功能。

#### 任务

- [x] 记录当前音频采集、播放、VAD、切句、ASR 和 Transcript 的调用链。
- [x] 列出并审计 `cockpit/drivers/alsa` 的直接和间接 CMake 依赖。
- [x] 审计 `cockpit/modules/audio` target 和 `cockpit/library/driver/audio` 职责。
- [x] 检查并收缩 `audio.proto` RPC。
- [x] 识别并迁移 `modules/audio/vad`、`audio_service`、`speech_output`、
  `SubscribeTranscripts` 和 `Speak(text)` 等混层代码。
- [x] 保存构建和测试基线；详细批次证据见[变更记录](变更记录.md)。

#### 验收

- [x] 输出当前依赖图和迁移清单。
- [x] 当前主分支能够正常构建。
- [x] 审计阶段未修改功能行为。

---

### 阶段 1：清理 ALSA 驱动依赖

#### 目标

让 `cockpit/drivers/alsa` 只依赖音频基础接口，不再间接依赖 VAD 等上层模块。

#### 任务

- [x] 拆分并确认 `audio_frames`、`audio_capture`、`audio_playback`、`audio_analysis` 和
  `audio_wav` 基础 target。
- [x] 移除 ALSA driver 对 `audio` 聚合 target 和所有上层 target 的依赖。最终实现是 driver
  只链接系统 ALSA，上层音频能力精确依赖 driver：

```cmake
target_link_libraries(alsa_audio
    PUBLIC
        PkgConfig::ALSA
)

target_link_libraries(audio_capture PUBLIC audio_frames alsa_audio)
target_link_libraries(audio_playback PUBLIC audio_wav alsa_audio)
```

- [x] 确认 `drivers/alsa` 不链接 `audio_vad`、`modules/voice`、Sherpa-ONNX 或 ONNX Runtime。
- [x] 保持 ALSA 采集、播放和 XRun 恢复行为不变。

#### 验收

- [x] `drivers/alsa` 的依赖树中不存在 VAD、ASR、TTS。
- [x] ALSA 采集和播放测试通过。
- [x] 主项目构建通过。
- [x] CMake 依赖方向符合：

```text
Linux / ALSA
     ↓
drivers/alsa
     ↓
modules/audio
     ↓
library/driver/audio
```

箭头表示上层依赖下层。`drivers/alsa` 不链接 `core`、`modules`、`library` 或 `agent`；CI 的
driver dependency boundary 检查持续保护该约束。

---

### 阶段 2：缩减 Audio Driver 职责

#### 目标

让 `cockpit/library/driver/audio` 只负责 PCM 采集、播放和传输。

#### 任务

- [x] 将原 `audio_service` 拆分为职责明确的 Runtime、Capture、Playback、Transport 和 gRPC 组件。
- [x] 当前结构遵循以下职责划分（文件名以仓库实际实现为准）：

```text
cockpit/library/driver/audio/
├── audio_runtime.cc
├── capture/
│   └── audio_capture.cc
├── playback/
│   └── audio_playback.cc
├── transport/
│   ├── audio_stream_server.cc
│   └── audio_ring_buffer.cc
└── grpc/
    └── audio_grpc_server.cc
```

- [x] 从 Audio Driver 中移除 VAD、SpeechSegmenter、ASR、Transcript 和 TTS 文本输入。
- [x] 保留 StartCapture、StopCapture、GetStatus、PCM 输出和 PCM 播放。
- [x] `SetVolume` 因当前没有真实 consumer，不进入活动 RPC；需要硬件音量控制时再设计契约。
- [x] 自有生命周期类使用 `*Runtime`、`*Controller`、`*Playback` 或 `*GrpcServer` 等职责名。

#### 验收

- [x] Audio Driver 不生成 Transcript，也不接收 TTS 文本。
- [x] Audio Driver 可以独立完成 PCM 采集和播放。
- [x] `cockpit/library/driver/audio` 不依赖顶层 `agent/` 或 Sherpa-ONNX。

---

### 阶段 3：建立 Driver 到 Agent 的 PCM 传输

#### 目标

建立清晰、稳定的 PCM 数据边界，让 Agent 从 Audio Driver 获取音频。

#### 任务

- [x] 第一版确定为 Unix `SOCK_SEQPACKET`：帧大小固定、边界天然保留，复杂度和拷贝成本在
  16 kHz 单声道场景可控；不为当前吞吐量引入共享内存协议。
- [x] 定义 PCM 数据格式：

```text
sample_rate: 16000
channels: 1
sample_format: PCM_S16LE
frame_duration: 10 ms 或 20 ms
sequence_number
monotonic_timestamp
```

- [x] 定义缓冲区满、读端落后、重连和设备重启行为。
- [x] 支持 Agent 消费 PCM，并通过 `AudioControl.PlayPcm` 提交播放 PCM。
- [x] 提供序号、采集时间戳、丢帧、队列和重连指标。
- [x] PCM 协议不包含 Transcript、VAD、ASR 或 TTS 语义。

#### 验收

- [x] Agent 能持续读取 PCM并提交播放数据。
- [x] Driver 和 Agent 任一侧重启后可以恢复。
- [x] 传输积压不会无限增长内存。
- [x] 协议时间戳和指标支持测量端到端 PCM 延迟；真机延迟门槛仍待声学验收。

---

### 阶段 4：创建顶层 Agent 基础结构

#### 目标

建立独立的 Agent 应用层，承接全部语音和交互逻辑。

#### 任务

- [x] 创建顶层 `agent/` 和当前有真实实现的子目录：

```text
agent/
├── runtime/
├── speech/
├── interaction/
├── conversation/
├── actions/
└── vehicle/
```

- [x] 将 Navigator 薄入口收口到 `cockpit/navigator/library/agent/agent_entry.cc`；测试统一放在
  根目录 `tests/agent/`，不创建空目录占位。
- [x] 建立 Agent Runtime 初始化、启动、停止和停机生命周期。
- [x] 建立 PCM 输入和播放输出接口。
- [x] 本阶段不接入具体模型。

#### 验收

- [x] Navigator 能通过 `agent_entry` 启动和停止 Agent。
- [x] Navigator 薄入口不包含具体语音算法。
- [x] Agent 能接收模拟 PCM 并向 Audio Driver 播放测试 PCM。

---

### 阶段 5：迁移 VAD 与 SpeechSegmenter

#### 目标

将 VAD 和切句逻辑从音频模块或驱动流程迁移到 Agent。

#### 任务

- [x] 从 `cockpit/modules/audio/vad` 移出 VoiceActivityDetector、SpeechSegmenter 和旧动态插件层。
- [x] 产品接口与实现当前统一放在 Agent 应用层：

```text
agent/speech/
├── vad/
└── segment/
```

- [x] SpeechSegmenter 保留 pre-roll、最短语音、静音结束、最长时长和异常截断职责。
- [x] 删除 Energy VAD 正式运行路径；RMS、峰值和削波只保留为 `AudioLevelMeter` 诊断。
- [x] 删除 VAD/ASR 算法动态插件 ABI；具体实现通过普通 C++ 接口注入 Agent 产品 target。

#### 真实 Provider 候选参数

以下参数属于后续真实 VAD 标定候选，不是当前活动 YAML：

```yaml
vad:
  threshold: 0.50
  min_speech_ms: 250
  min_silence_ms: 600
  pre_roll_ms: 300
  max_speech_ms: 15000
```

#### 验收

- [x] Audio Driver 不再引用 VAD。
- [x] Agent 使用 mock VAD 能从 PCM 中切出完整语句，自动化边界测试通过。
- [ ] 语音开头和结尾不明显截断。
- [x] 超长语音能够强制结束。
- [ ] 背景噪声不会导致无限 LISTENING。

---

## 阶段 6–10：语音交互主链路

状态：阶段 6 已正式封板；阶段 7 KWS 主链路已起步；阶段 8 仅 Sherpa KWS provider 边界代码已落地；
阶段 9 尚未开始；阶段 10 第一批已完成但整体仍在进行。基础仓库继续
使用 mock provider；本册中的模型、版本和性能项目只有在独立 Agent 产品构建与 Jetson 实测完成后
才能勾选。

### 阶段 6：建立会话状态机和恢复机制

#### 目标

统一管理 KWS、VAD、ASR、LLM、TTS 和动作执行状态。

#### 状态

```text
IDLE
  ↓
WAKING
  ↓
LISTENING
  ↓
RECOGNIZING
  ↓
ROUTING
  ├─ EXECUTING
  └─ THINKING
       ↓
     SPEAKING
       ↓
    FOLLOW_UP
       ↓
      IDLE
```

异常状态：

```text
ERROR_RECOVERY
CANCELLED
SHUTTING_DOWN
```

#### 任务

- [x] 定义状态、事件和允许的状态转换，并拒绝非法转换。
- [x] 一次只允许一个活动语音会话。
- [x] 当前已有 consumer 的分环节预算进入严格活动配置：

```yaml
features:
  ai:
    asr_timeout_ms: 3000
    assistant_timeout_ms: 10000
    command_execution_timeout_ms: 3000
    tts_synthesis_timeout_ms: 5000
    follow_up_window_ms: 8000
```

- [x] 删除通用 `request_timeout_ms`；ASR、Assistant、Action 和 TTS 分别消费 steady-clock deadline，
  Gateway/HMI 将调用方剩余预算转换为 gRPC deadline。
- [x] provider 失败、超时和主动打断统一执行取消、清空过期队列、停止当前响应并恢复；非用户主动
  失败通过原输出通道播放固定错误提示，提示完成后才返回 `IDLE`。
- [x] `PlayPcm accepted` 只表示有界队列接收；真实 `player_->Play()` 返回后，Audio Driver 通过
  playback id 保存 `completed / failed / cancelled / dropped` 最终结果。
- [x] Agent 异步等待单次播放结果，`completed` 驱动 `SPEAKING -> FOLLOW_UP`；播放失败进入恢复，
  播放取消不进入 `FOLLOW_UP`。
- [x] `FOLLOW_UP` 使用 8 秒可配置 monotonic 窗口；窗口内 transcript 进入新请求，超时返回
  `IDLE`，interrupt/shutdown 和新 generation 会使旧完成回调与 timer 失效。
- [x] 通用超时恢复：取消当前任务、停止播放、清空当前语句、使旧 generation 失效、播放固定错误
  提示并在其 terminal completion 后返回 `IDLE`。真实模型的内部状态重置仍由后续 provider 的
  `Cancel()` 实现负责。
- [x] 禁止多个 ASR、LLM 或 TTS 请求并发执行。

尚未进入活动配置的规划预算保持不变：`wait_for_speech_ms` 等 KWS/LISTENING timer 实现后再加入；
`llm_first_token_ms`、`llm_total_ms`、`tts_first_audio_ms` 和流式 TTS 总预算等真实 streaming provider
出现后再确定。`features.voice.speech_segment.max_segment_ms` 已承担语音段上限，不重复增加
`max_speech_ms`。

#### 验收

- [x] 当前 provider 超时后能取消工作，通过固定提示保持 `ERROR_RECOVERY` active，并在提示结束后
  恢复到 `IDLE`。
- [ ] 真实模型进程异常不会阻塞 Navigator 主循环。
- [x] 不出现同时监听、识别和播放的非法状态。
- [x] 状态转换、打断、失败恢复和停机终态具备单元测试。
- [x] 播放 accepted/completed 分离、真实完成、失败、取消、stale completion、FollowUp 超时和
  窗口内续问具备单元测试。
- [x] accepted playback 的 Timeout/TransportError 会执行有界取消与 terminal confirmation；取消
  失败最多重试两次，最终不确定时明确失败且不会伪报 `Cancelled`。
- [x] 同一 playback id 的 Stop/Interrupt/uncertainty 取消使用 single-flight；只有明确失败后才允许
  有界重试，完成回调保持 exactly once。
- [x] Action 使用单请求 cancellation context；Interrupt 在 dispatcher/provider/RPC 各入口均可见，
  pre-dispatch 取消不会再发送旧 HMI/vehicle RPC。
- [x] SpeechPipeline Stop 通过生命周期 generation 拒绝 provider 在 Cancel 后返回的 stale success。
- [x] driver dependency boundary 在缺少 `rg`、向上 include 或链接项目 target 时确定性失败，CI 不再
  产生假通过。

---

### 阶段 7：接入 KWS 与唤醒反馈

#### 目标

加入持续唤醒能力和明确用户反馈。

#### 模型

```text
sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20
```

#### 任务

- [x] 第一阶段只配置一个唤醒词，内容可通过 `wake_word` 或 `keywords_file` 自定义。
- [x] 建立 KWS 接口和 mock 实现。
- [x] 建立 `VoiceInputGate`：
  - [x] KWS disabled 时保持 PCM 进入 `SpeechPipeline` 的旧行为。
  - [x] KWS enabled + `Idle` 时 PCM 只进入 KWS。
  - [x] `Listening` / `FollowUp` 时 PCM 只进入 `SpeechPipeline`。
  - [x] `Waking` / `Recognizing` / `Routing` / `Executing` / `Thinking` / `Speaking` 等状态暂停输入处理。
- [x] KWS 命中后进入 `Waking`，固定 PCM 提示音完成后进入 `Listening`。
- [ ] UI 图标、状态灯或动画反馈接入真实 HMI consumer。
- [x] 第一版明确不支持“唤醒词和命令连说”，KWS 命中后 reset KWS 和 speech input state。
- [x] 增加唤醒冷却时间，防止连续触发。
- [x] TTS 播放期间第一阶段暂停 KWS/VAD/ASR。
- [x] 新增 `voice_input_gate_test` 覆盖 disabled 直通、Idle KWS、Listening speech、cooldown 和 wake transition。

#### 验收

- [ ] 正常距离可稳定唤醒。
- [ ] 音乐和系统 TTS 不频繁误唤醒。
- [ ] 长时间运行能够统计每小时误唤醒次数。
- [x] 代码链路中唤醒后可播放固定 PCM 声音反馈。
- [ ] Jetson 真实音频链路中确认用户能立即获得声音或界面反馈。

---

### 阶段 8：接入 Sherpa Agent 产品实现

#### 目标

在同一仓库的 Agent 产品构建中恢复真实语音模型能力，但不污染基础系统默认 CMake、CI 和
Audio Driver 依赖。算法实现使用普通 C++ 接口注入，不恢复逐算法 `dlopen` 插件 ABI。

#### 目录

```text
源码：
agent/speech/providers/sherpa/

本地产品资源：
_output/ai/
├── runtime/sherpa-onnx/v1.13.4/
├── models/kws/
├── models/vad/
├── models/asr/
└── config/kws-keywords.txt

车端产品资源：
/cockpit-system/ai/
├── runtime/sherpa-onnx/v1.13.4/
├── models/kws/
├── models/vad/
├── models/asr/
└── config/kws-keywords.txt
```

目录表示目标责任边界，不提前创建空骨架。模型文件、第三方源码和运行库不提交到主仓库。

#### 固定版本

```text
Sherpa-ONNX：v1.13.4
VAD：Sherpa 官方 silero_vad.onnx
ASR 回退：SenseVoiceSmall INT8 2024-07-17
ASR 候选：Qwen3-ASR-0.6B INT8
TTS：kokoro-multi-lang-v1_1
```

#### 任务

- [x] 用 `WakeWordDetector` 普通 C++ 接口实现 Sherpa KWS provider。
- [x] 用 `VoiceActivityDetector` 普通 C++ 接口实现 Sherpa Silero VAD provider 代码。
- [x] 用 `SpeechRecognizer` 普通 C++ 接口实现 Sherpa SenseVoiceSmall INT8 provider 代码。
- [x] 增加默认关闭的 `COCKPIT_ENABLE_SHERPA_AGENT` 产品构建开关，基础 CI 不要求 Sherpa runtime/model。
- [x] Sherpa KWS provider 对外只暴露项目接口，不传播 Sherpa 或 ONNX Runtime 类型。
- [x] 增加 `_output/ai` 本地资源布局和 `prepare-sherpa-runtime.sh`、
  `prepare-voice-models.sh` 准备入口；运行时和模型不进 Git、不在程序启动时下载。
- [x] 增加 Sherpa Silero VAD 与 SenseVoiceSmall INT8 provider 代码骨架，仍需产品构建和 Jetson smoke
  验证。
- [ ] 真实执行 `COCKPIT_ENABLE_SHERPA_AGENT=ON` 产品构建。
- [ ] 固定 Sherpa-ONNX v1.13.4 runtime 交付物，并使用其私有 ONNX Runtime；不得要求它与应用 gRPC 共用
  Protobuf。
- [ ] 使用 `-fvisibility=hidden`。
- [ ] 使用 linker version script。
- [ ] 使用 `$ORIGIN` 相对 RPATH。
- [ ] 保留 Navigator module 边界的 `--exclude-libs,ALL` 和 version script，限制
  Sherpa/ONNX Runtime 第三方符号。
- [x] 主项目不出现 Sherpa、ONNX Runtime 和模型下载逻辑。
- [x] Ubuntu x86_64 VM 下的真实 Sherpa smoke 入口已固化为 `bash scripts/tests/sherpa-voice-smoke.sh`。

#### 验收

- [x] 基础系统只依赖稳定领域接口，Sherpa KWS 算法实现仅由 Agent 产品 target 链接。
- [x] 主项目 CMake 中不存在 `find_package(ONNXRuntime)`。
- [ ] Agent 产品依赖和模型可以按固定版本独立准备、构建和发布。
- [ ] Jetson 完成 KWS → wake cue → Silero VAD → SenseVoice smoke。
- [ ] provider 初始化失败时 Agent 能明确降级并恢复。
- [ ] 若同进程符号或故障隔离实测不可靠，再记录升级为独立进程的条件。

---

### 阶段 9：接入 ASR 并完成对比

#### 目标

恢复离线整句 ASR，并比较 SenseVoice 与 Qwen3-ASR。

#### 方案

```text
流式 PCM
  ↓
Silero VAD
  ↓
SpeechSegmenter
  ↓
完整语音段
  ↓
非流式 ASR
```

#### 任务

- [ ] 接入 SenseVoiceSmall INT8 作为稳定回退。
  - 当前已有 Sherpa SenseVoice provider 代码骨架和固定 `_output/ai/models/asr/sensevoice-small-int8`
    资源路径；产品构建、真实模型加载和 Jetson smoke 仍未完成。
- [ ] 接入 Qwen3-ASR-0.6B INT8 作为候选。
- [ ] 统一 ASR 输出结构。
- [ ] 开启 ITN 或文本规范化。
- [ ] 建立固定车控命令测试集。
- [ ] 测试：
  - [ ] 中文 CER
  - [ ] 命令整句准确率
  - [ ] 数字和单位准确率
  - [ ] 否定词准确率
  - [ ] 平均延迟
  - [ ] P95 延迟
  - [ ] 峰值 RSS
  - [ ] 连续识别稳定性

#### 验收

- [ ] ASR 只接收切好的完整语音段。
- [ ] 车控只使用最终 ASR 文本。
- [ ] Qwen3-ASR 只有在关键指标不下降时才能替换 SenseVoice。
- [ ] 普通 CER 提升不能抵消车控命令准确率下降。

---

### 阶段 10：实现确定性命令路由

#### 目标

确保车控命令不经过 LLM。

#### 路由

```text
transcript
  ↓
TranscriptNormalizer
  ↓
DeterministicCommandRouter
  ├─ 正向命令整句白名单命中 → 现有 VoiceAction
  │                         ↓
  │                    ActionDispatcher
  │
  └─ 其他所有 transcript → Unknown + None
```

未来 LLM 只能消费未命中的普通问答文本并生成回复或澄清，不持有 `ActionDispatcher`，不能调用 Shell、
发送 CAN、执行任意 RPC 或把自由文本转换为车辆动作。所有硬件动作仍必须来自这里的确定性白名单。

#### 任务

- [x] 增加最小 `TranscriptNormalizer`：trim、空白折叠、ASCII lowercase、全角 ASCII/U+3000
  转换和常见中英文标点分隔，同时保留中文 UTF-8。
- [x] `DeterministicCommandRouter` 第一批只允许已有的 `OpenCamera`、`PlayMusic` 和
  `QueryVehicleStatus`，不增加没有 consumer 的 Action 或参数结构。
- [x] 规范化 transcript 只与固定 `CommandEntry` 正向白名单做整句相等匹配，不使用自由文本
  substring、否定词 blacklist、规则语法或通用 matcher。
- [x] 否定、条件、延期、意愿、多动作组合和未支持参数等所有非白名单输入统一 fail closed，
  不能退化成任一已有硬件 Action。
- [x] `MockVoiceAssistant` 保持原接口和 deadline 行为，内部复用 normalizer 和 router。
- [ ] 出现真实 consumer 后再评审以下候选类型化动作，当前不进入 `VoiceAction`：

```text
Stop
CameraOpen
CameraClose
RecordingStart
RecordingStop
QueryVehicleStatus
QueryBatteryStatus
LightOn
LightOff
```

- [ ] 基于固定真实 ASR 语料扩展同义词和误识别词归一化。
- [ ] 只有参数化 Action 及 consumer 落地后，才建立数字、单位解析和参数范围检查。
- [ ] 建立设备状态和权限检查。
- [x] 第一批确定性边界禁止 LLM 控制车辆、Shell、任意 RPC、动态插件调用和自由生成车辆运动参数。
- [ ] `LocalLlmClient` 不得持有 `ActionDispatcher`。

#### 验收

- [x] 当前 `MockVoiceAssistant` 的动作只能来自 `DeterministicCommandRouter`。
- [ ] LLM 输出无法进入动作执行接口。
- [x] 全部正向白名单以及否定、多动作、英文子串、自然语言嵌入和未支持车辆参数测试通过。
- [x] 阶段 10 第一批正式封板；出现真实 consumer 或固定 ASR 语料前不增加规则。
- [ ] 语音停止不替代底层急停和安全状态机。

---

## 阶段 11–15：模型接入与产品化

状态：阶段 11 进行中，阶段 12–15 未开始。功能接口可先在 Ubuntu x86_64 验证，性能与声学指标
仍必须来自固定模型、固定配置和 Jetson 真机。

### 阶段 11：接入本地 LLM

#### 目标

为未命中确定性命令的文本提供本地开放问答。

#### 基线

```text
运行时：llama.cpp
生产候选：Qwen3.5-2B GGUF Q4_K_M
对照候选：Qwen3.5-4B GGUF Q4_K_M
```

#### 初始参数

```text
context：2048 或 4096
max_tokens：128
parallel：1
thinking：关闭
GPU offload：尽可能全部
监听地址：127.0.0.1
```

#### 任务

- [x] 固定并验证 llama.cpp `b10456` / `f275595dd16f7ed3d644d4d8159b14b305960479`。
- [x] 使用 Agent 托管的独立 `llama-server` 进程，不增加 systemd service。
- [x] Agent 本地 LLM 访问已抽象为 `LocalLlmClient`，业务层不直接依赖具体推理实现。
- [x] 已有 `LlamaServerLocalLlmClient` 原型，按 OpenAI-compatible HTTP 请求到本地 server。
- [x] Agent runtime 已通过 `features.ai.local_llm` 创建 client，只通过本地接口获取回复文本；默认关闭。
- [x] 增加 llama.cpp runtime、GGUF 模型准备脚本和真实 llama-server smoke 入口。
- [x] 完成 readiness、周期 health、异常退出/失健康重启、指数退避上限和进程组清理。
- [x] 通过 chunked/非 chunked SSE 增量消费并聚合 Token 流式输出。
- [x] 分别设置首 Token 和总响应超时，取消时关闭当前请求 socket。
- [x] 请求不启用工具调用，只发送 system/user 文本消息。
- [x] Qwen3.5 请求通过 `chat_template_kwargs.enable_thinking=false` 关闭 thinking，推理字段不进入播报正文。
- [x] `LocalLlmClient` 只返回回复文本，不持有 Shell、RPC、`ActionDispatcher` 或车辆接口。
- [ ] 在相同参数下对比 Qwen3.5-2B 与 4B 的回答质量、首 token、tokens/s 和 RSS。
- [x] Ubuntu x86_64 CPU 完成 2B/4B 真实功能 smoke；Jetson ARM64/CUDA 和性能结论仍待真机。

#### 验收

- [ ] 无网络时可以运行。
- [x] LLM 只处理 deterministic router 未命中的 Unknown；已有命令隔离测试保持通过。
- [x] LLM 接口只能生成回复文本，无法构造或下发 `VoiceAction`。
- [ ] 与 ASR、摄像头并发时不发生 OOM。
- [x] 具备请求超时取消和进程重启能力；真实模型压力验证待完成。

---

### 阶段 12：接入 TTS 与播放

#### 目标

实现动态回答播报，同时保留低延迟固定提示音。

#### 基线

```text
动态 TTS：kokoro-multi-lang-v1_1
固定提示：预录 WAV
升级实验：Qwen3-TTS-12Hz-0.6B-CustomVoice
```

#### 任务

- [ ] 固定提示音直接走 Audio Playback。
- [ ] 动态回复通过 Kokoro 生成 PCM。
- [ ] LLM 输出按句号、问号或长度切分。
- [ ] TTS 分句生成，ALSA 边生成边播放。
- [ ] 播放队列设置容量上限。
- [ ] 第一阶段播放期间暂停 KWS、VAD 和 ASR。
- [ ] Qwen3-TTS 作为独立实验实现，不进入第一阶段 Sherpa 产品 target。

#### 验收

- [ ] 固定提示不依赖 TTS 模型。
- [ ] 动态回复可以正常中英混读。
- [ ] 空文本、异常字符和长文本不会导致崩溃。
- [ ] 连续播放不存在明显内存增长。
- [ ] TTS 失败时能够回退到固定提示。

---

### 阶段 13：音频前处理与打断

#### 第一部分：NS 和 AGC

- [ ] 接入 WebRTC NS。
- [ ] 使用真实风扇、电机和车内录音测试。
- [ ] 记录开启前后的 ASR 和 KWS 指标。
- [ ] AGC 仅在麦克风增益不足时启用。
- [ ] 避免硬件 AGC 和软件 AGC 重复工作。

#### 第二部分：AEC 和 Barge-in

后续阶段：

- [ ] 接入 WebRTC AEC3。
- [ ] 将播放 PCM 作为远端参考信号。
- [ ] 恢复 TTS 播放期间的 KWS/VAD。
- [ ] 支持用户打断 TTS。
- [ ] 定义打断后的 LLM、TTS 和会话清理行为。

#### 验收

- [ ] NS 不显著降低唤醒和识别准确率。
- [ ] AEC 开启后系统 TTS 不频繁触发自身。
- [ ] 用户打断后能够停止播放并进入 LISTENING。
- [ ] 不出现旧 TTS 音频继续播放的问题。

---

### 阶段 14：模型版本、升级和回滚

#### 目标

模型和运行时可追踪、可验证、可回滚。

#### 任务

- [ ] 每个模型记录：
  - [ ] family
  - [ ] model_id
  - [ ] model_file
  - [ ] quantization
  - [ ] SHA-256
  - [ ] runtime
  - [ ] runtime_version
  - [ ] provider_version
  - [ ] config_version
- [ ] LLM 额外记录：
  - [ ] 基础模型。
  - [ ] GGUF 转换来源。
  - [ ] GGUF 文件哈希。
  - [ ] llama.cpp commit。
  - [ ] context 和量化配置。
- [ ] 禁止：
  - [ ] 使用 `latest`。
  - [ ] 启动时自动下载。
  - [ ] 自动覆盖生产模型。
  - [ ] 跟随 master。
- [ ] 设备保留：

```text
current
previous
candidate
```

#### 升级流程

```text
候选模型
  ↓
格式、ABI、许可证检查
  ↓
离线固定测试集
  ↓
Jetson 单模型测试
  ↓
Jetson 全系统压力测试
  ↓
人工确认
  ↓
发布生产版本
```

#### 验收

- [ ] 模型切换不需要重新编译主项目。
- [ ] 升级失败可以恢复上一版本。
- [ ] 模型、运行时和配置均可追踪。
- [ ] 不存在自动替换生产模型的逻辑。

---

### 阶段 15：完整回归与发布门槛

#### KWS

- [ ] 真实唤醒率。
- [ ] 假拒绝率。
- [ ] 每小时误唤醒次数。
- [ ] 远距离唤醒率。
- [ ] 音乐和 TTS 回放误唤醒率。

#### VAD

- [ ] 开头截断时长。
- [ ] 结尾截断时长。
- [ ] 噪声误触发率。
- [ ] 漏检率。
- [ ] 平均切句延迟。
- [ ] 长停顿错误断句率。

#### ASR

- [ ] 中文 CER。
- [ ] 车控命令整句准确率。
- [ ] 动作分类准确率。
- [ ] 数字和单位准确率。
- [ ] 否定词准确率。
- [ ] 平均和 P95 延迟。
- [ ] 峰值 RSS。
- [ ] 长时间稳定性。

#### TTS

- [ ] 首包延迟。
- [ ] RTF。
- [ ] 中文自然度。
- [ ] 中英混读。
- [ ] 数字和单位读法。
- [ ] 异常文本稳定性。
- [ ] 长时间内存变化。

#### LLM

- [ ] 首 Token 延迟。
- [ ] tokens/s。
- [ ] 峰值共享内存。
- [ ] 连续对话稳定性。
- [ ] 开放问答质量。
- [ ] 越权请求拒绝率。
- [ ] 与 ASR、TTS、摄像头并发稳定性。

#### 发布否决条件

出现任一情况禁止升级：

- [ ] 车控命令准确率下降。
- [ ] 否定词准确率下降。
- [ ] P95 延迟超过预算。
- [ ] 峰值内存导致 OOM 风险。
- [ ] 长时间运行存在持续内存增长。
- [ ] provider 接口或运行时版本不兼容。
- [ ] 许可证不符合要求。
- [ ] 无法一键回滚。

---

## 语音阶段实施顺序

更新时间：2026-08-17。阶段 0–6 已完成，阶段 6、阶段 10 第一批已正式封板但整体未完成；
阶段 7–9 等待 Jetson 声学/性能验收，阶段 11 在 Ubuntu VM 并行推进，不再继续扩展阶段 6。

```text
阶段 0  现状冻结
阶段 1  ALSA 依赖清理
阶段 2  Audio Driver 职责缩减
阶段 3  PCM 传输
阶段 4  顶层 Agent 基础结构
阶段 5  VAD 与切句迁移
阶段 6  会话状态机
阶段 7  KWS 和唤醒反馈
阶段 8  Sherpa Agent 产品实现
阶段 9  ASR 对比
阶段 10 确定性命令路由
阶段 11 本地 LLM
阶段 12 TTS
阶段 13 NS、AEC 和打断
阶段 14 模型升级与回滚
阶段 15 全量回归与发布
```

### 当前优先级

当前优先完成：

```text
已完成：
分层纠正、CMake 依赖清理、Audio Driver 缩减、PCM 传输、顶层 agent/ 建立、
VAD/Segmenter 工程迁移、阶段 6 会话状态机、分环节 deadline、恢复和生命周期 hardening、
阶段 7 KWS input gate / cooldown / 可配置唤醒词、阶段 10 第一批 TranscriptNormalizer 与确定性正向整句白名单、
阶段 11 llama-server client/进程托管/SSE/deadline/cancel

P1：
固定 llama.cpp commit、源码归档哈希和 Qwen3.5 2B/4B GGUF 哈希
Ubuntu VM 真实 llama-server/Qwen3.5-2B smoke，并运行 4B 对照
Jetson Sherpa KWS smoke
Sherpa Agent 产品实现剩余 provider
SenseVoice 恢复
Qwen3-ASR 对比

P2：
Kokoro TTS

P3：
WebRTC NS
Qwen3-TTS 实验
AEC3
Barge-in
模型自动化回归
```
