# Cockpit 车载语音与本地 Agent 架构

## 1. 文档状态

本文描述 Jetson Orin Nano 8GB 的目标架构、当前实现和候选模型。三者必须明确区分：

- **当前实现**：已经存在于 `cockpit-system`，由 CI 验证。
- **目标基线**：准备在 Jetson 真机上验收，未通过前不视为生产能力。
- **候选实验**：只用于比较，不进入默认配置和安装包。

最后更新：2026-07-30。

## 2. 已确定的边界

1. 所有语音和 LLM 能力默认本地离线运行。
2. 车控命令走确定性匹配和类型化白名单，不交给 LLM 决策。
3. LLM 只生成回复文本，不持有 `ActionDispatcher`，不能执行 Shell、任意 RPC 或车辆动作。
4. Sherpa-ONNX、ONNX Runtime、llama.cpp 和模型不进入主项目 CMake。
5. 第三方语音运行时通过稳定 C ABI 插件接入，使用 `RTLD_NOW | RTLD_LOCAL`。
6. `cockpit-system` 只安装一个 systemd service；外部推理进程由 Navigator 模块启动和监管。
7. 模型、运行时和配置均固定版本，设备启动时不联网下载或自动升级。
8. 第一阶段采用半双工：TTS 播放期间暂停 KWS、VAD 和 ASR。

## 3. 当前实现

| 能力 | 当前状态 |
| --- | --- |
| ALSA PCM16 采集与播放 | 已实现 |
| 16 kHz / mono / 20 ms 音频帧 | 已实现 |
| 音频环形缓冲 | 已实现 |
| RMS、峰值和削波诊断 | 已实现 |
| `SpeechSegmenter` | 已实现 |
| VAD C ABI 和插件加载器 | 已实现 |
| ASR C ABI 和插件加载器 | 已实现 |
| Energy VAD | 已移除 |
| Sherpa-ONNX/SenseVoice | 主仓库中不存在 |
| KWS | 未实现 |
| 真实 VAD 插件 | 未实现 |
| 真实 ASR 插件 | 未实现 |
| TTS | 只有 mock |
| 本地 LLM client | 未实现 |
| 完整语音会话状态机 | 未实现 |

默认配置保持：

```yaml
services:
  audio:
    vad:
      provider: disabled

features:
  voice:
    enabled: false
    asr:
      provider: mock
```

未安装真实 VAD 插件时，不得通过配置伪装成生产语音链路。

## 4. 目标数据流

```text
ALSA 采集
  ↓
格式校验 / 重采样 / 单声道转换
  ↓
有界音频缓冲
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
    │   └── dlopen(libcockpit-speech-sherpa.so, RTLD_LOCAL)
    ├── agent
    │   └── llama-server 子进程
    └── 其他 Navigator 模块
```

`llama-server` 不增加独立 systemd service。`agent` 模块负责：

- 启动固定版本的 `llama-server`；
- 记录 PID 和进程组；
- readiness、health 和请求 deadline；
- 异常退出退避重启；
- 停止时清理剩余进程组。

Navigator 的 child subreaper 和进程组清理能力继续作为兜底。

## 6. 语音插件

目标包与主项目独立发布：

```text
/cockpit-system/plugins/speech/
├── releases/
│   └── 1.0.0/
│       ├── lib/
│       │   ├── libcockpit-speech-sherpa.so
│       │   ├── libsherpa-onnx-c-api.so
│       │   └── libonnxruntime.so
│       ├── config/
│       │   └── speech.yaml
│       ├── manifest.yaml
│       └── checksums.sha256
├── current -> releases/1.0.0
└── previous -> releases/0.9.0
```

同一个 `libcockpit-speech-sherpa.so` 可以导出多个 API 表，但主项目按能力分别加载：

```c
CockpitVadPluginGetApi();
CockpitAsrPluginGetApi();
```

KWS 和 TTS ABI 在出现第二个真实实现、完成调用语义设计后再增加，当前不预先冻结。

### 6.1 ABI 约束

- ABI 使用 C 类型，不跨边界传递 STL、C++ 异常或所有权不清晰的内存。
- API 表包含 `abi_version` 和 `struct_size`，未来字段只能追加。
- 输入缓冲区由 host 持有，仅在当前调用期间有效。
- 插件写入 host 提供的输出和错误缓冲区。
- 插件必须声明实例是否允许并发调用；第一阶段按单线程实例使用。
- 成功初始化的插件不执行 `dlclose()`，但模型实例仍可 `destroy()`。
- 编译使用 `-fvisibility=hidden`、version script 和 `$ORIGIN` 相对 RPATH。
- 静态第三方符号使用 `--exclude-libs,ALL` 隐藏。

### 6.2 当前 VAD ABI

VAD 插件接收任意长度 PCM chunk，可以在内部缓冲。20 ms host frame 与 Silero 推荐窗口不一致时，
由插件负责拼接，不得把模型窗口要求泄漏进 `AudioService`。

VAD 输出：

```text
silence / speech
speech_probability [0, 1]
status
error
```

状态跳变由 host adapter 计算，`SpeechSegmenter` 继续负责 pre-roll、最大句长、截断和 discontinuity。

### 6.3 当前 ASR ABI

ASR 接收 VAD 切出的完整语音段。`recognize()` 当前是同步调用：

- 交互层超时只能丢弃迟到结果，属于软超时；
- 不能声称可以中断正在执行的 ONNX Runtime 推理；
- 如果后续必须提供硬超时和崩溃隔离，应升级成受监管的独立语音进程。

## 7. 依赖隔离

```text
cockpit-system
└── Ubuntu 系统 Protobuf/gRPC

libcockpit-speech-sherpa.so
└── Sherpa-ONNX
    └── 私有 ONNX Runtime 及其内部依赖

llama-server
└── llama.cpp + CUDA + GGUF
```

主仓库禁止出现：

```text
find_package(ONNXRuntime)
add_subdirectory(sherpa-onnx)
SHERPA_ONNX_ENABLE_*
SenseVoice/Qwen 模型下载逻辑
llama.cpp 源码或模型下载逻辑
```

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

语音插件与模型分开版本化：

```text
/cockpit-system/plugins/speech/current
/cockpit-system/plugins/speech/previous
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
abi_version: 1
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
- 迟到结果丢弃和插件异常恢复。

### LLM

- 首 token 延迟和 tokens/s；
- 4K context 下共享内存峰值；
- 输出长度限制；
- 注入、越权和伪造工具调用；
- 摄像头、ASR、TTS 同时运行时的稳定性。

模型榜单分数不能代替车内固定数据集和真机稳定性。

## 14. 实施顺序

1. **主仓库边界**：移除 Energy VAD，落地 VAD C ABI、插件加载器和音频诊断。
2. **独立语音包**：固定 Sherpa-ONNX/ONNX Runtime，先实现 Silero VAD。
3. **ASR 对比**：SenseVoice 与 Qwen3-ASR 在 Jetson 上做固定数据集验收。
4. **KWS 与会话**：接入 Zipformer KWS、半双工状态机和预录提示音。
5. **命令安全链**：实现 normalizer、matcher、typed action 和 validator。
6. **本地 LLM**：由 agent 模块监管 llama-server，先接 Qwen3-4B-Instruct-2507。
7. **动态 TTS**：验证 Kokoro INT8 的延迟和内存后接入句级流水线。

每一步单独提交、测试和验收，不把模型下载或第三方构建重新塞回主项目。

## 15. 官方参考

- Sherpa-ONNX：https://github.com/k2-fsa/sherpa-onnx
- Sherpa-ONNX C API：https://k2-fsa.github.io/sherpa/onnx/c-api/html/
- KWS 模型：https://k2-fsa.github.io/sherpa/onnx/kws/pretrained_models/
- Silero/TEN VAD：https://k2-fsa.github.io/sherpa/onnx/c-api/html/vad.html
- Qwen3-ASR：https://k2-fsa.github.io/sherpa/onnx/c-api/html/offline_asr.html
- Kokoro：https://k2-fsa.github.io/sherpa/onnx/tts/pretrained_models/kokoro.html
- llama.cpp：https://github.com/ggml-org/llama.cpp
- Qwen3-4B-Instruct-2507：https://huggingface.co/Qwen/Qwen3-4B-Instruct-2507
