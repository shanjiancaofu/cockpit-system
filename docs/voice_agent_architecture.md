# Cockpit 车载语音 Agent 架构与实施计划

## 1. 文档状态

本文是语音 Agent 的唯一总体文档，合并原《语音与 AI 规划》。文档严格分为两部分：

- **第一部分——架构**：长期稳定的分层、进程、数据流、安全和资源边界。
- **第二部分——实施计划**：按阶段记录已完成项、当前工作和未来验收门槛。

文档中的能力状态使用以下定义：

- **当前实现**：已经存在于 `cockpit-system`，由 CI 验证。
- **目标基线**：准备在 Jetson 真机上验收，未通过前不视为生产能力。
- **候选实验**：只用于比较，不进入默认配置和安装包。

最后更新：2026-08-13。

# 第一部分：稳定架构

## 2. 已确定的边界

1. 所有语音和 LLM 能力默认本地离线运行。
2. 车控命令走确定性匹配和类型化白名单，不交给 LLM 决策。
3. LLM 只生成回复文本，不持有 `ActionDispatcher`，不能执行 Shell、任意 RPC 或车辆动作。
4. `cockpit/drivers` 只负责硬件适配，`cockpit/library/driver` 只负责驱动模块组装和数据传输。
5. 顶层 `agent/` 负责 VAD、语音分段、ASR、LLM、工具调用和 TTS。
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
| Sherpa-ONNX/SenseVoice | 主仓库中不存在 |
| KWS | 未实现 |
| 真实 VAD 实现 | 未实现 |
| 真实 ASR 实现 | 未实现 |
| TTS 与 PCM 回放 | mock TTS 已在 Agent，Driver 只播放 PCM |
| 本地 LLM client | 未实现 |
| 语音会话状态机 | 状态/事件核心已实现，KWS 和真实 provider 事件待接入 |

默认配置保持：

```yaml
features:
  voice:
    enabled: false
    vad:
      provider: mock
    speech_segment:
      pre_roll_ms: 100
      max_segment_ms: 15000
    asr:
      provider: mock
```

未接入真实 VAD 和 ASR 时，不得通过配置伪装成生产语音链路。

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
Agent 有界音频缓冲
  ↓
KWS（流式）
  ↓
VAD（流式）
  ↓
SpeechSegmenter
  ↓
完整语音段
  ↓
ASR（非流式）
  ↓
TranscriptNormalizer
  ↓
CommandMatcher
  ├─ 命中 → TypedAction → ActionValidator → ActionDispatcher
  └─ 未命中 → LocalLlmClient → 回复文本 → TTS → ALSA
```

车控只使用最终 ASR 结果，禁止根据 ASR 中间结果执行动作。

## 5. 进程与所有权

设备仍然只有一个 systemd 单元：

```text
cockpit-system.service
└── cockpit-navigator
    ├── audio_driver
    │   ├── ALSA Capture/Playback
    │   └── PCM Stream Publisher
    ├── agent
    │   ├── VAD / Segmenter / ASR / TTS
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
├── Sherpa-ONNX
└── 私有 ONNX Runtime 及其内部依赖

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

Agent 产品 target 使用普通 C++ 接口链接具体实现，并通过模块进程边界、hidden visibility、
version script 和 `--exclude-libs,ALL` 限制第三方符号。只有出现外部供应商独立交付且必须
运行时替换的需求时，才重新评估算法插件 ABI。

## 8. 模型候选

以下名称是候选清单，不表示已经通过本项目验收。

### 8.1 KWS

目标候选：

```text
sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20
```

第一阶段只配置一个唤醒词。必须测试车内音乐、TTS 回放、远场、开窗风噪和每小时误唤醒次数。

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
llama.cpp 固定 commit
Qwen3-4B-Instruct-2507
GGUF Q4_K_M
context 2048/4096
并发 1
最大输出 128
```

该模型原生为 non-thinking，不需要解析 `<think>`。

升级实验：

```text
Qwen3.5-4B GGUF Q4_K_M
```

Qwen3.5-4B 是多模态模型，当前阶段只测文本，不加载视觉投影。社区 GGUF 的来源、转换 commit、
SHA-256 和许可证必须记录。vLLM 只做真机对比，Ollama 不使用。

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
  ↓ KWS
WAKING
  ↓ 短提示音
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

## 11. 车控安全边界

```text
TranscriptNormalizer
  ↓
CommandMatcher（精确语法、同义词、否定词）
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
/cockpit-system/agent/current
/cockpit-system/agent/previous
/cockpit-system/models/speech/current
/cockpit-system/models/speech/previous
/cockpit-system/models/llm/current
/cockpit-system/models/llm/previous
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

# 第二部分：分阶段实施计划

## 14. 阶段规则

- 阶段状态只使用“已完成”、“进行中”、“未开始”和“真机待验收”。
- 一个阶段必须同时满足代码、单元测试、构建和文档验收，才能标记完成。
- 基础 CI 不下载模型，不构建 Sherpa-ONNX、ONNX Runtime 或 llama.cpp。
- 每个阶段单独提交，不在架构重构提交中夹带模型和部署包。
- Jetson 相关性能结论只能由真机数据产生，WSL2 只负责通用逻辑和工程质量。

## 15. 阶段总览

| 阶段 | 目标 | 状态 | 交付物 |
| --- | --- | --- | --- |
| 0 | 现状冻结与依赖审计 | 已完成 | 调用链、迁移清单、构建基线 |
| 1 | ALSA 依赖精确化 | 已完成 | ALSA 只链接音频基础 target |
| 2 | Audio Driver 职责缩减 | 已完成 | 采集、PCM 发布/播放、设备状态 |
| 3 | Driver 到 Agent PCM 传输 | 已完成 | `SOCK_SEQPACKET` 协议、有界队列、重连和丢帧指标 |
| 4 | 顶层 Agent 基础结构 | 已完成 | `agent/`、Runtime、PCM 输入和播放输出 |
| 5 | VAD、切句、ASR、TTS 迁入 Agent | 已完成 | 普通 C++ 接口、mock 实现和流水线测试 |
| 6 | 会话状态机与恢复 | 进行中 | 显式状态/事件、转换校验、单会话和超时恢复 |
| 7 | KWS 和唤醒反馈 | 未开始 | KWS 接口、冷却、预录提示音、半双工 |
| 8 | Sherpa 产品实现 | 未开始 | 独立产品构建、固定版本、私有 ONNX Runtime |
| 9 | 真实 ASR 对比 | 未开始 | SenseVoice 基线与 Qwen3-ASR 真机报告 |
| 10 | 确定性命令路由 | 未开始 | normalizer、matcher、validator、typed action |
| 11 | 本地 LLM | 未开始 | 受监管 `llama-server`、超时、取消和文本输出 |
| 12 | 动态 TTS | 未开始 | Kokoro PCM、句级合成、有界播放队列 |
| 13 | NS、AEC 和 Barge-in | 未开始 | 车内噪声抑制、回声消除和打断 |
| 14 | 模型版本、升级和回滚 | 未开始 | manifest、签名、`current/previous` 原子切换 |
| 15 | 全量回归与发布门槛 | 未开始 | 真机指标、稳定性和发布否决条件 |

## 16. 已完成基线（阶段 0–5）

已经落地的端到端链路是：

```text
ALSA 麦克风
  → audio_driver（采集并发布 PCM）
  → Unix SOCK_SEQPACKET
  → agent（mock VAD → SpeechSegmenter → mock ASR）
  → transcript / intent / typed action
  → response text → mock TTS
  → AudioControl.PlayPcm
  → audio_driver（只播放 PCM）
  → ALSA 扬声器
```

已删除 VAD/ASR 动态插件 ABI、加载器、`Speak(text)` 和 Audio RPC 上的
Transcript 订阅。Transcript 由 `VoiceInteractionControl.SubscribeTranscripts` 发布。

## 17. 当前阶段：会话状态机（阶段 6）

当前进度：状态/事件核心、非法转换拒绝、单会话串行、打断、provider 错误恢复、
停机终态、Voice RPC 状态和状态转换指标已实现。待 KWS 和真实 ASR/LLM/TTS provider 接入后，
再完成分环节 deadline、播放完成回执和 `FOLLOW_UP` 窗口。因此本阶段保持“进行中”。

### 17.1 实现范围

1. 在 `agent/conversation/` 建立独立会话状态机，不把转换规则散落在 gRPC、
   Audio Driver 或具体模型实现中。
2. 定义 `IDLE`、`WAKING`、`LISTENING`、`RECOGNIZING`、`ROUTING`、`EXECUTING`、
   `THINKING`、`SPEAKING`、`FOLLOW_UP`、`CANCELLED`、`ERROR_RECOVERY` 和
   `SHUTTING_DOWN`。
3. 转换 API 拒绝非法边，保留最后一次转换原因和统计。
4. 一次只允许一个活动会话；新请求不能和 ASR、LLM、TTS 并发执行。
5. 打断、超时和 provider 异常统一经过 `CANCELLED/ERROR_RECOVERY`，执行取消、
   清队列、停播放和恢复 `IDLE`。
6. 先使用通用 `request_timeout_ms` 保持配置兼容；KWS、ASR、LLM 和 TTS 真实
   provider 接入时，再按实际阶段拆分 deadline。

### 17.2 验收

- 所有允许和禁止转换具备单元测试。
- 打断、provider 失败和超时最终都恢复到 `IDLE`。
- 停止 Agent 后状态是 `SHUTTING_DOWN`，不得重新接收 transcript。
- Voice RPC 状态输出能区分会话阶段，不再只暴露 `listening/processing`。
- Debug、Release、ASan/UBSan 和 CI 保持通过。

## 18. 后续阶段边界

### 阶段 7：KWS 与唤醒反馈

建立 KWS 接口、单唤醒词、冷却时间、预录提示音和 UI 反馈。第一版采用半双工，
TTS 播放期间暂停 KWS/VAD/ASR。

### 阶段 8–9：Sherpa 产品实现与 ASR 对比

Sherpa-ONNX 使用固定版本的独立产品构建，ONNX Runtime 及其内部依赖保持私有。
主项目 CMake 不查找 ONNX Runtime，基础 CI 不下载 Sherpa 或模型。默认不恢复
算法级 `dlopen` C ABI；只有存在外部供应商独立替换的确定需求时，才重新评审该边界。

### 阶段 10：确定性命令路由

实现 TranscriptNormalizer、CommandMatcher、ActionValidator 和否定词/参数范围测试。
所有车控动作必须来自确定性路由。

### 阶段 11–12：本地 LLM 与动态 TTS

Agent 监管固定 commit 的 `llama-server`，只通过本地接口获取回复文本。动态 TTS 按句
合成 PCM 并走 Audio Playback；固定提示优先使用预录 WAV。

### 阶段 13：音频前处理与打断

先用真实车内录音验证 NS/AGC，再接 AEC3 和 Barge-in。开启 AEC 前继续保持
半双工，不得让系统 TTS 触发自身。

### 阶段 14–15：模型生命周期与发布

实现可追踪 manifest、校验和签名、`current/previous` 原子切换、固定车内数据集、
Jetson 全系统压力测试和发布否决条件。

## 19. 官方参考

- Sherpa-ONNX：https://github.com/k2-fsa/sherpa-onnx
- Sherpa-ONNX C API：https://k2-fsa.github.io/sherpa/onnx/c-api/html/
- KWS 模型：https://k2-fsa.github.io/sherpa/onnx/kws/pretrained_models/
- Silero/TEN VAD：https://k2-fsa.github.io/sherpa/onnx/c-api/html/vad.html
- Qwen3-ASR：https://k2-fsa.github.io/sherpa/onnx/c-api/html/offline_asr.html
- Kokoro：https://k2-fsa.github.io/sherpa/onnx/tts/pretrained_models/kokoro.html
- llama.cpp：https://github.com/ggml-org/llama.cpp
- Qwen3-4B-Instruct-2507：https://huggingface.co/Qwen/Qwen3-4B-Instruct-2507
