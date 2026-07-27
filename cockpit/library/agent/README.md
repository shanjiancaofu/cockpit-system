# agent 目录职责

`cockpit/library/agent` 是语音交互与后续 AI 能力的进程级编排入口。当前实现没有接入
大语言模型，保留 `agent` 名称是为了稳定这一编排边界，而不是表示已经存在通用智能体。

当前处理链路：

```text
Audio 转写
  -> VoiceInteractionService
  -> MockVoiceAssistant
  -> Vehicle/HMI 动作
  -> 语音响应
  -> Audio 播放
```

子目录职责：

- `audio/`：接收转写和发送语音播放请求。
- `interaction/`：对话状态、请求排队、打断和响应编排。
- `vehicle/`：读取车辆状态并执行车辆相关动作。
- `hmi/`：执行本地 HMI 命令。
- `grpc/`：对外提供语音交互控制与状态接口。

未来接入 LLM、意图识别或工具调用时，应继续复用这条编排链路和既有安全边界，
不要把模型推理、设备驱动或底层音频处理直接堆入本目录。
