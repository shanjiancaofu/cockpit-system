# audio-service

Jetson 本地麦克风和扬声器所有者。

```text
audio-service/
├── capture/     采集生命周期模块
├── processing/  VAD、分段和 ASR 编排
├── playback/    TTS 队列和扬声器输出
├── grpc/        控制与状态 API
└── main.cc      进程装配
```

原始 PCM 保持进程内，通过 SPSC ring 进入 VAD/ASR。gRPC 只提供 start/stop/status、transcript
文本流和 `Speak(text)`。

默认 ASR/TTS 是 mock。启用 whisper.cpp 需要构建选项、源码目录和模型路径。真实 TTS 尚未接入。
