# modules

可测试、可复用、尽量平台无关的领域能力：

- `audio`：音频 frame、采集线程、VAD、分段、播放接口和 WAV。
- `camera`：相机 frame、采集接口和共享内存布局。
- `can`：平台无关 CAN frame。
- `sentinel`：哨兵状态机、触发策略、冷却和重复抑制。
- `vehicle`：Vehicle/Chassis 状态、正式 CAN codec 和平台无关 ChassisClient 聚合。
- `voice`：ASR、TTS、assistant、action 和 response。

较大模块按明确职责建立子目录和独立 target；小模块保持扁平。禁止使用 `base`、`misc`、
`common` 作为临时收纳目录。硬件 API 放在 `drivers`，进程级资源所有权放在 `library`。
Sentinel 不解析原始 CAN，也不持有 Camera/Recording 实现；SR501 电平采集和去抖属于 STM32。
