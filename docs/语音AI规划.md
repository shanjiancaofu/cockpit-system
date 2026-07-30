# 语音与 AI 规划

## 当前链路

```text
ALSA 麦克风
  → audio_driver（采集并发布 PCM）
  → Unix SOCK_SEQPACKET
  → agent（VAD → SpeechSegmenter → ASR）
  → transcript / intent / typed action
  → response text → TTS
  → AudioControl.PlayPcm
  → audio_driver（只播放 PCM）
  → ALSA 扬声器
```

当前基础仓库已完成 PCM 线协议、Driver Publisher、Agent Client、mock VAD/ASR/TTS、语音分段、
类型化动作和响应输出。`AudioControl` 不再提供文本 `Speak` 或 transcript stream；transcript
属于 Agent，并由 `VoiceInteractionControl.SubscribeTranscripts` 对外发布。

## 实现边界

- `cockpit/drivers/alsa`：ALSA 硬件适配。
- `cockpit/library/driver/audio`：采集、PCM 发布、PCM 播放和设备状态。
- `agent/speech`：VAD、分段、ASR、TTS 和语音流水线。
- `agent/interaction`：transcript、意图、动作和回复生命周期。
- `cockpit/modules/voice`：不依赖硬件的交互模型、动作合同和响应接口。

VAD、ASR 和 TTS 是 Agent 进程内普通 C++ 组件，不分别做运行时动态插件。Navigator 的
`libagent.so` 仍是进程级动态加载边界。

## 真实算法接入

基础构建和 CI 不包含 Sherpa-ONNX、ONNX Runtime、llama.cpp 或模型。Jetson 产品构建按固定版本
增加 Agent 实现 target：

```text
Agent
├── Sherpa-ONNX KWS / Silero VAD / ASR / TTS
├── 私有 ONNX Runtime 及内部依赖
└── llama.cpp llama-server client
```

项目 Protobuf/gRPC 使用系统依赖；ONNX Runtime 的 Protobuf 保持私有隔离。不得为强行统一版本
增加 Provider、Find 模块或兼容层。

## 动作安全

```text
transcript
  → deterministic command matcher / LLM proposal
  → allowlist validation
  → typed ActionDispatcher
  → Gateway / HMI
```

车控命令必须经过确定性白名单和状态校验。LLM 不直接访问 ALSA、CAN、摄像头、Shell 或任意 RPC。

## 演进顺序

1. 在 WSL 保持 Debug、Release、ASan/UBSan 和 CI 通过。
2. 在 Jetson 验证 USB 麦克风、扬声器和 PCM 端到端延迟。
3. 固定 Sherpa-ONNX/ONNX Runtime 版本，接入真实 KWS、Silero VAD 和 ASR。
4. 完成车内风噪、音乐、TTS 回放和远场声学测试。
5. 接入固定版本 llama.cpp，增加 deadline、内存上限和子进程监管。
6. 接入真实 TTS，并补齐打断、半双工和播放取消策略。
