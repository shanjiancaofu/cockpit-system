# 语音与 AI 规划

## 当前目标

先完成可靠的本地语音交互闭环，再增加大模型能力。语音功能属于用户交互；录像、录包和研发数据
采集属于独立 diagnostics/recording 边界，不混入语音动作。

## 当前链路

```text
麦克风
  → audio-service
  → VAD
  → SpeechSegmenter
  → mock ASR / whisper.cpp
  → transcript gRPC stream
  → voice-interaction-service
  → intent / action
  → response text
  → audio-service Speak RPC
  → mock TTS
  → 扬声器
```

当前已完成：

- ALSA 采集和播放。
- 20 ms PCM frame、SPSC ring、VAD 和语音分段。
- mock ASR、whisper.cpp adapter 和 WSL CPU 推理。
- transcript 订阅、重连、历史重放。
- 白名单意图和类型化 ActionDispatcher。
- 车辆状态查询、打开相机和播放音乐 HMI handoff。
- 异步 TTS 队列、取消和健康指标。

## Provider 边界

### ASR

统一实现 `SpeechRecognizer`：

- `mock`：测试和 smoke 默认实现。
- `whisper_cpp`：当前首个真实实现。
- 后续可选：sherpa-onnx + SenseVoice、Qwen ASR、TensorRT adapter。

ASR 在 `audio-service` 进程内运行，避免语音 PCM 跨进程传输。

### TTS

统一实现 `SpeechSynthesizer`。当前 mock 只生成测试音频，后续可接：

- 本地轻量中文 TTS。
- Jetson TensorRT/ONNX provider。
- 远程云 TTS。

### LLM

LLM 应位于 `voice-interaction-service`，输入是 transcript 和结构化上下文，输出是回复文本或受控
工具调用。LLM 不直接访问 ALSA、CAN、摄像头或 shell。

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

## 演进顺序

1. Jetson 麦克风和扬声器标定。
2. Whisper 中文真实语音测试和性能测量。
3. 替换真实 TTS。
4. 加入 push-to-talk 完整 UI。
5. 增加唤醒词、AEC 和打断。
6. 引入 LLM provider 和受控工具调用。
7. 根据 Jetson 性能评估 SenseVoice/Qwen ASR 等替代方案。
