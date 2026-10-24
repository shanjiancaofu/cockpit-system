# voice 模块

```text
voice/
├── assistant/  transcript、意图和回复模型
├── actions/    类型化动作分发
└── responses/  异步回复输出
```

对应 target：`voice_assistant`、`voice_actions`、`voice_responses`。父级 `voice`
是兼容聚合。VAD、语音分段、ASR 和 TTS 位于顶层 `agent/speech`。

voice module 不直接访问 ALSA、CAN、摄像头或 shell。硬件和服务调用通过明确接口注入。
