# agent

`agent/` 是座舱智能交互应用层，对应 Navigator 管理的 `agent` 模块。

职责包括：

- 语音活动检测、语音分段、识别和合成；
- 对话状态、LLM 客户端和工具调用；
- 将结构化动作请求交给 Gateway/HMI 做最终校验和执行。

本目录不直接访问 ALSA、V4L2、SocketCAN 等硬件接口。音频输入和输出通过
`cockpit/modules/audio/transport` 定义的中立协议与驱动模块交换。

当前基础实现包含 PCM 流客户端、mock VAD/ASR/TTS、语音分段和交互流水线，不引入
Sherpa-ONNX、ONNX Runtime、模型或第三方算法依赖。真实算法后续作为 Agent 产品构建中的普通
CMake target 接入，不增加 VAD/ASR 运行时插件层。
