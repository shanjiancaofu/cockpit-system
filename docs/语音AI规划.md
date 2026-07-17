# 语音与 AI 规划

## 当前目标

先完成可靠的本地语音交互闭环，再增加大模型能力。语音功能属于用户交互；录像、录包和研发数据
采集属于独立 diagnostics/recording 边界，不混入语音动作。

## 当前链路

```text
麦克风
  → audio_driver
  → VAD
  → SpeechSegmenter
  → mock ASR / whisper.cpp
  → transcript gRPC stream
  → agent
  → intent / action
  → response text
  → audio_driver Speak RPC
  → mock TTS
  → 扬声器
```

当前已完成：

- ALSA 采集和播放。
- 20 ms PCM frame、SPSC ring、VAD 和语音分段。
- mock ASR 和 whisper.cpp adapter；WSL-R4 已完成真实模型 CPU 基线，默认构建仍未启用 whisper.cpp。
- transcript 订阅、重连、历史重放。
- 白名单意图和类型化 ActionDispatcher。
- 车辆状态查询通过 gateway gRPC 真实执行。
- 打开相机通过本地 HMI gRPC 切换 Qt Camera 页面，并返回真实执行结果。
- 播放音乐已进入同一类型化合同，但媒体未接入时明确返回失败。
- 异步 TTS 队列、取消和健康指标。

## Provider 边界

### ASR

统一实现 `SpeechRecognizer`：

- `mock`：测试和 smoke 默认实现。
- `whisper_cpp`：当前首个真实实现。
- 后续可选：sherpa-onnx + SenseVoice、Qwen ASR、TensorRT adapter。

ASR 在 `audio_driver` module child 内运行，避免语音 PCM 跨进程传输。

### TTS

统一实现 `SpeechSynthesizer`。当前 mock 只生成测试音频，后续可接：

- 本地轻量中文 TTS。
- Jetson TensorRT/ONNX provider。
- 远程云 TTS。

### LLM

LLM 应位于 `agent` 的 Assistant provider 边界，输入是 transcript 和结构化上下文，输出是回复文本或受控
工具调用。LLM 不直接访问 ALSA、CAN、摄像头或 shell。

当前活动配置只保留已有 consumer 的 `features.voice.enabled`、ASR 参数和
`features.ai.request_timeout_ms`。`features.voice.mode`、`features.voice.tts_provider`、
`features.ai.provider` 与 `features.ai.model` 是未来候选契约；在对应的第二种真实实现和可切换
装配逻辑落地前，不进入 `configs/config.yaml`，避免配置看似可选而实际始终运行 mock。

## 动作安全

```text
transcript
  → intent / LLM tool proposal
  → allowlist validation
  → typed ActionDispatcher
  → gateway / HMI bridge
```

- 查询类动作可直接执行。
- 车控类动作需要权限、状态条件和确认机制。
- 禁止模型生成任意 shell 命令。
- 所有动作保留结果状态和可诊断消息。

## Android 与媒体

音乐播放通常由 Android/Qt 媒体应用负责。C++ Runtime 只产生 `play_music`、`pause_music` 等 HMI
命令，未来通过明确的 bridge 交给 UI/Android，不在 voice module 内实现完整播放器。

当前 `LocalHmiCommandProvider` 通过本地 Unix-socket gRPC 调用 cockpit-ui。`open_camera` 只有在 Qt
主线程完成页面状态切换后才成功；媒体命令继续等待播放器责任边界明确，不在 voice module 内实现。

## 演进顺序

1. 已完成：mock 打断、连续命令、队列丢弃、超时和 provider 失败恢复。
2. 已完成 WSL-R4：whisper.cpp `6fc7c33b` 与 `ggml-small.bin` 在 GCC Release 下识别 16 kHz mono
   JFK WAV，耗时 4.39 秒、CPU 393%、峰值 RSS 649232 KiB；源码、模型和 WAV 均不进入仓库。
3. Jetson 麦克风、扬声器、AEC、增益和唤醒/打断标定。
4. 播放器责任边界明确后补 Qt/Android 媒体动作和 push-to-talk UI。
5. 根据 Jetson 性能选择真实 TTS 和 SenseVoice/Qwen ASR 等替代方案。
6. 后端 provider 合同明确后引入可取消、有 deadline 的 LLM 和受控工具调用。
