# Cockpit 车载语音 Agent 架构

## 1. 文档状态

本文只维护语音 Agent 长期稳定的分层、进程、数据流、安全和资源边界。实施状态与阶段任务见
[语音阶段任务入口](语音Agent阶段任务.md)，避免架构和计划各维护一份进度。

文档中的能力状态使用以下定义：

- **当前实现**：已经存在于 `cockpit-system`，由 CI 验证。
- **目标基线**：准备在 Jetson 真机上验收，未通过前不视为生产能力。
- **候选实验**：只用于比较，不进入默认配置和安装包。

最后更新：2026-08-15。

## 2. 已确定的边界

1. 所有语音和 LLM 能力默认本地离线运行。
2. 车控命令走确定性匹配和类型化白名单，不交给 LLM 决策。
3. LLM 只生成回复文本，不持有 `ActionDispatcher`，不能执行 Shell、任意 RPC 或车辆动作。
4. `cockpit/drivers` 只负责硬件适配，`cockpit/library/driver` 只负责驱动模块组装和数据传输。
5. 顶层 `agent/` 负责 KWS、VAD、语音分段、ASR、LLM、工具调用和 TTS。
6. Navigator 只动态加载顶层模块，Agent 内部算法使用普通 C++ target 和接口注入，不再逐个
   设计为运行时插件。
7. Sherpa-ONNX、ONNX Runtime、llama.cpp 和模型只进入 Agent 产品构建，不进入基础系统的
   默认构建和 CI。
8. `cockpit-system` 只安装一个 systemd service；外部推理进程由 Navigator 模块启动和监管。
9. 模型、运行时和配置均固定版本，设备启动时不联网下载或自动升级。
10. 第一阶段采用半双工：TTS 播放期间暂停 KWS、VAD 和 ASR。

## 3. 当前实现

| 能力 | 当前状态 |
| --- | --- |
| ALSA PCM16 采集与播放 | 已实现 |
| 16 kHz / mono / 20 ms 音频帧 | 已实现 |
| 音频环形缓冲 | 已实现 |
| RMS、峰值和削波诊断 | 已实现 |
| `SpeechSegmenter` | 已实现 |
| PCM 线协议 | 已实现第一版 |
| Driver 侧 PCM Publisher | 已实现并接入采集链路 |
| Agent 侧 PCM Client | 已实现并接入语音流水线 |
| VAD/ASR 插件加载器 | 已删除 |
| Energy VAD | 已移除 |
| Sherpa-ONNX/SenseVoice | 不进入默认构建；Sherpa provider 仅在显式 Agent 产品构建中编译 |
| KWS | 接口、mock 实现、input gate、cooldown、异步固定 PCM wake prompt 和 Sherpa KWS provider 代码已落地；Jetson 真机声学验收待完成 |
| 真实 VAD 实现 | Sherpa Silero VAD provider 代码骨架已落地；产品构建和 Jetson smoke 待完成 |
| 真实 ASR 实现 | Sherpa SenseVoiceSmall INT8 provider 代码骨架已落地；产品构建和 Jetson smoke 待完成 |
| TTS 与 PCM 回放 | mock TTS 已在 Agent，Driver 只播放 PCM |
| 本地 LLM client | `LocalLlmClient`、llama-server SSE、首 token/总超时、进程托管和真实 server smoke 入口已实现；固定 runtime/model 实测待完成 |
| 语音会话状态机 | 状态/事件核心已实现，KWS wake 事件已通过 `VoiceInteractionService` 公开入口接入 |

默认配置保持：

```yaml
features:
  voice:
    enabled: false
    kws:
      enabled: false
      provider: mock
      cooldown_ms: 1500
      wake_word: 你好小车
      model_dir: ""
    vad:
      provider: mock
    speech_segment:
      pre_roll_ms: 100
      max_segment_ms: 15000
    asr:
      provider: mock
```

未完成 Jetson 真实模型加载和声学 smoke 前，不得把这些 provider 代码骨架伪装成生产语音链路。

## 4. 目标数据流

```text
ALSA 采集
  ↓
Audio Driver 格式校验 / 单声道 PCM
  ↓
有界发送队列
  ↓
Unix SOCK_SEQPACKET PCM Stream
  ↓
Agent AudioStreamClient
  ↓
VoiceInputGate
  ├─ Idle → KWS（流式）
  ├─ Listening / FollowUp → VAD（流式）
  └─ Waking / Recognizing / Routing / Executing / Thinking / Speaking → Paused
  ↓
SpeechSegmenter
  ↓
完整语音段
  ↓
ASR（非流式）
  ↓
TranscriptNormalizer
  ↓
DeterministicCommandRouter
  ├─ 命中 → TypedAction → ActionValidator → ActionDispatcher
  └─ 未命中 → LocalLlmClient → 回复文本 → TTS → ALSA
```

车控只使用最终 ASR 结果，禁止根据 ASR 中间结果执行动作。

## 5. 进程与所有权

设备仍然只有一个 systemd 单元：

```text
cockpit-navigator.service
└── cockpit-navigator
    ├── audio_driver
    │   ├── ALSA Capture/Playback
    │   └── PCM Stream Publisher
    ├── agent
    │   ├── KWS / VAD / Segmenter / ASR / TTS
    │   └── llama-server 子进程
    └── 其他 Navigator 模块
```

`audio_driver` 不认识 VAD、Transcript、ASR、TTS 或 LLM。它只发布采集 PCM、接收播放 PCM
并维护设备健康状态。`agent` 通过中立音频线协议消费 PCM，算法不反向依赖驱动实现。

`llama-server` 不增加独立 systemd service。`agent` 模块负责：

- 启动固定版本的 `llama-server`；
- 记录 PID 和进程组；
- readiness、health 和请求 deadline；
- 异常退出退避重启；
- 停止时清理剩余进程组。

Navigator 的 child subreaper 和进程组清理能力继续作为兜底。

当前已完成 client、配置和 server 子进程托管。`features.ai.local_llm.enabled` 默认关闭；显式选择
`llama-server` 时必须同时启用 `manage_process`，Agent 只连接 loopback endpoint。托管器负责资源
预检、独立进程组、readiness、周期 health、指数退避重启上限和有界退出回收。确定性命令仍先经过
allowlist router。client 增量解析 chunked/非 chunked SSE，并将 token 聚合为回复文本；首 token deadline
和总回复 deadline 独立，取消时主动关闭当前 socket。固定 runtime/model 实测仍属于阶段 11 后续任务。

## 6. Driver 与 Agent 音频协议

第一版使用 Unix `SOCK_SEQPACKET`：

```text
audio_driver
  └── /cockpit-system/run/audio-capture.sock
          ↓ PCM16 / 16 kHz / mono / 20 ms
agent
```

协议使用固定长度、显式小端序字段，包含：

```text
magic
protocol version
message type
packet size
frame flags
sequence
monotonic capture timestamp
sample rate / channels / sample count
PCM samples
```

采集线程不直接执行 socket I/O。Driver 使用有界队列和独立 Publisher 线程；队列满时丢弃旧帧，
下一帧携带 `discontinuity` 和 `dropped-before`。socket 文件权限为 `0600`，停止时只删除自身
创建且 inode 匹配的 socket。

播放方向使用 Audio gRPC 的有界 `PlayPcm` 请求。文本合成在 Agent 内完成，Audio Driver
只校验 PCM16/16 kHz/mono 和负载大小并异步播放，不接收 `Speak(text)`，也不持有 TTS 实现。

## 7. 依赖隔离

```text
audio_driver 进程
└── Ubuntu 系统 Protobuf/gRPC + ALSA

agent 进程
├── 应用 Protobuf/gRPC
├── 默认构建：mock KWS/VAD/ASR/TTS
└── Agent 产品构建：Sherpa-ONNX + 私有 ONNX Runtime 及其内部依赖

llama-server
└── llama.cpp + CUDA + GGUF
```

基础系统默认构建禁止出现：

```text
find_package(ONNXRuntime)
add_subdirectory(sherpa-onnx)
SHERPA_ONNX_ENABLE_*
启动时模型下载逻辑
```

普通构建只编译 `WakeWordDetector` 接口、mock detector 和 input gate；`provider=sherpa` 需要显式
`COCKPIT_ENABLE_SHERPA_AGENT=ON`，并指定准备好的 Sherpa runtime/model root。Agent 产品 target
使用普通 C++ 接口链接具体实现，并通过模块进程边界、hidden visibility、
version script 和 `--exclude-libs,ALL` 限制第三方符号。只有出现外部供应商独立交付且必须
运行时替换的需求时，才重新评估算法插件 ABI。

## 8. 模型候选

以下名称是候选清单，不表示已经通过本项目验收。

### 8.1 KWS

目标候选：

```text
sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20
```

第一阶段只配置一个唤醒词，但唤醒词内容可以自定义：

```yaml
features:
  voice:
    kws:
      enabled: true
      provider: sherpa
      cooldown_ms: 1500
      wake_word: ""
      keywords_file: /cockpit-system/ai/config/kws-keywords.txt
      model_dir: /cockpit-system/ai/models/kws/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20
```

`wake_word` 只供 mock provider 使用；Sherpa provider 必须使用 tokenized `keywords_file`。普通 CI
使用 mock provider 验证 gate/cooldown，不需要 Sherpa/ONNX Runtime；Jetson 产品构建再使用
Sherpa KWS provider 和 `/cockpit-system/ai` 外部模型目录。必须测试车内音乐、TTS 回放、远场、
开窗风噪和每小时误唤醒次数。

### 8.2 VAD

目标基线：

```text
Sherpa-ONNX + silero_vad.onnx
```

对比候选：

```text
TEN-VAD
```

音量诊断与 VAD 已解耦，RMS/峰值不能用于生产语音判定。

### 8.3 ASR

稳定参考：

```text
SenseVoiceSmall INT8 2024-07-17
```

已有 Jetson 参考结果：

```text
5.59 秒音频
识别约 0.42 秒
RTF 约 0.075
采样峰值 RSS 约 337 MiB
```

升级候选：

```text
sherpa-onnx-qwen3-asr-0.6B-int8-2026-03-25
```

该模型文件本身接近 1 GiB，必须在 Jetson 上与 SenseVoice 比较命令准确率、否定词、P95 延迟、
冷启动、常驻内存和全系统压力后再决定。

### 8.4 LLM

产品候选：

```text
llama.cpp b10456 / f275595dd16f7ed3d644d4d8159b14b305960479
Qwen3.5-2B
GGUF Q4_K_M
context 2048/4096
并发 1
最大输出 128
thinking 关闭
```

对照候选：

```text
Qwen3.5-4B GGUF Q4_K_M
```

2B 是 Jetson Orin Nano 8GB 的默认生产候选，优先保证与 KWS、ASR、TTS、摄像头并发时的内存余量。
4B 只用于回答质量、首 token、tokens/s、RSS 和全系统压力对照，不进入默认配置。当前阶段两者都只
使用文本能力，不加载视觉投影。GGUF 的来源、转换 commit、SHA-256 和许可证必须分别记录；对照结论
必须来自相同 prompt、context、量化和运行时。vLLM 只做真机对比，Ollama 不使用。
客户端通过 llama.cpp b10456 支持的 `chat_template_kwargs.enable_thinking=false` 关闭 thinking，
只将 SSE `delta.content` 送入用户回复或 TTS；`reasoning_content` 仅用于识别异常空正文，不能播报。

### 8.5 TTS

目标候选：

```text
kokoro-int8-multi-lang-v1_1
Sherpa-ONNX OfflineTts
```

固定提示优先预录 WAV。动态回复采用句级流水线：

```text
LLM 产生完整句子 → 离线合成该句 → 播放该句 → 并行准备下一句
```

这不等于模型内部流式 TTS。Qwen3-TTS 保留为独立实验，不进入第一阶段。

## 9. Jetson 8GB 资源约束

Jetson 内存由 CPU 和 GPU 共享。第一轮真机测试采用以下上限作为工程护栏，之后用测量值修订：

| 类别 | 初始上限 |
| --- | ---: |
| 系统、Navigator 和普通模块 | 1.5 GiB |
| 摄像头、GStreamer 和音频缓冲 | 1.0 GiB |
| llama.cpp 4B Q4、KV cache 和 CUDA | 3.0 GiB |
| KWS、VAD、ASR 和语音运行时 | 1.2 GiB |
| TTS 临时工作集 | 0.3 GiB |
| 不可侵占安全余量 | 1.0 GiB |

规则：

- KWS、VAD 常驻；
- ASR 是否常驻由冷启动与内存实测决定；
- TTS 模型延迟创建，合成完成后允许销毁实例；
- 第一阶段不允许 ASR、LLM、TTS 多会话并发；
- 同时运行摄像头、ASR、LLM、TTS 的压力测试必须保留至少 1 GiB 可用内存；
- 使用 `tegrastats`、`/proc/*/smaps_rollup` 和 coredump 验证，不能只看单个进程 RSS。

## 10. 会话状态机

```text
IDLE
  ↓ KWS 命中
WAKING
  ↓ 固定 PCM 短提示音完成
LISTENING
  ↓ VAD endpoint
RECOGNIZING
  ↓ final transcript
ROUTING
  ├─ command → EXECUTING → IDLE/FOLLOW_UP
  └─ open query → THINKING → SPEAKING → IDLE/FOLLOW_UP
```

异常状态：

```text
CANCELLED
ERROR_RECOVERY
SHUTTING_DOWN
```

第一阶段一次只允许一个会话。TTS 播放期间暂停采集链路的 KWS/VAD/ASR 判定；AEC 和 barge-in
放到后续阶段。

当前 gate 状态表：

| 状态 | 输入处理 |
| --- | --- |
| `Idle` | KWS |
| `Listening`、`FollowUp` | SpeechPipeline |
| `Waking`、`Recognizing`、`Routing`、`Executing`、`Thinking`、`Speaking`、`Cancelled`、`ErrorRecovery`、`ShuttingDown` | Paused |

KWS 关闭时保持开发兼容模式：所有 PCM 直接进入 `SpeechPipeline`。

## 11. 车控安全边界

```text
TranscriptNormalizer
  ↓
DeterministicCommandRouter（normalized full transcript exact positive allowlist）
  ↓
TypedAction
  ↓
ActionValidator（模式、权限、车辆状态）
  ↓
ActionDispatcher
```

LLM 未命中的请求只能进入回复通道。即使模型产生工具调用或伪造 JSON，也没有代码路径可以到达
`ActionDispatcher`。

语音“停止”只是上层辅助请求，不能替代物理急停、底盘安全状态机、通信超时和硬件看门狗。
提示音使用 `stop_ack.wav`，不得命名为 `emergency_stop.wav`。

## 12. 版本、升级和回滚

Agent 运行时与模型分开版本化：

```text
/cockpit-system/releases/
/cockpit-system/current
/cockpit-system/ai/runtime/
/cockpit-system/ai/models/
/cockpit-system/ai/config/
```

每个 manifest 至少记录：

```yaml
name: ""
version: ""
runtime_version: ""
runtime_commit: ""
model_id: ""
model_file: ""
quantization: ""
sha256: ""
license: ""
config_version: 1
```

发布包必须带校验和和签名。切换 `current` 前完成 ABI、架构、哈希、许可证和最小 smoke test；
升级失败时原子恢复 `previous`。

禁止使用 `latest`、启动时下载、跟随 master 或自动覆盖当前模型。

## 13. 验收

### VAD

- 语音开头/结尾截断；
- 噪声误触发和漏检；
- 长停顿断句；
- TTS 回放和音乐误触发；
- 连续运行内存增长；
- discontinuity、丢帧和 reset。

### ASR

- 中文 CER；
- 车控整句和动作分类准确率；
- “打开/不要打开”“停止/不要停止”等否定词；
- 数字、单位和中英混读；
- 平均/P95 延迟、冷启动和峰值内存；
- 迟到结果丢弃和算法异常恢复。

### LLM

- 首 token 延迟和 tokens/s；
- 4K context 下共享内存峰值；
- 输出长度限制；
- 注入、越权和伪造工具调用；
- 摄像头、ASR、TTS 同时运行时的稳定性。

模型榜单分数不能代替车内固定数据集和真机稳定性。

## 14. 相关文档

- [当前实现状态](实现状态.md)
- [语音阶段任务入口](语音Agent阶段任务.md)
- 当前工作：在任务表中定位“阶段 6：建立会话状态机和恢复机制”。

## 15. 官方参考

- Sherpa-ONNX：https://github.com/k2-fsa/sherpa-onnx
- Sherpa-ONNX C API：https://k2-fsa.github.io/sherpa/onnx/c-api/html/
- KWS 模型：https://k2-fsa.github.io/sherpa/onnx/kws/pretrained_models/
- Silero/TEN VAD：https://k2-fsa.github.io/sherpa/onnx/c-api/html/vad.html
- Qwen3-ASR：https://k2-fsa.github.io/sherpa/onnx/c-api/html/offline_asr.html
- Kokoro：https://k2-fsa.github.io/sherpa/onnx/tts/pretrained_models/kokoro.html
- llama.cpp：https://github.com/ggml-org/llama.cpp
- Qwen 官方模型：https://huggingface.co/Qwen
