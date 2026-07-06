# voice-interaction-service

用户语音交互编排进程。

```text
voice-interaction-service/
├── interaction/  transcript、intent 和 response 主流程
├── audio/        transcript 订阅和 Speak client
├── vehicle/      gateway 车辆状态 client
├── hmi/          Qt/Android handoff
├── grpc/         状态和调试 API
└── main.cc       进程装配
```

服务不打开麦克风或扬声器，也不处理原始 PCM。它消费 transcript，执行白名单 intent 和类型化
action，再把回复文本发送给 audio-service。

录音、录像和研发录包不属于本服务。
