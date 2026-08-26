# modules

可测试、可复用、尽量平台无关的领域能力：

- `audio`：音频 frame、采集线程、VAD、分段、播放接口和 WAV。
- `camera`：相机 frame、采集接口和共享内存布局。
- `hawkeye`：平台无关的视觉域基础数据模型；当前只包含 Camera Calibration 严格配置加载。
- `bridge`：Cockpit 与未来 ROS2/Nav2 adapter 之间的平台无关 goal、pose、状态机和 fake provider。
- `can`：平台无关 CAN frame。
- `sentinel`：哨兵状态机、触发策略、冷却和重复抑制。
- `vehicle`：Vehicle/Chassis 状态、正式 CAN codec 和平台无关 ChassisClient 聚合。
- `voice`：ASR、TTS、assistant、action 和 response。

较大模块按明确职责建立子目录和独立 target；小模块保持扁平。禁止使用 `base`、`misc`、
`common` 作为临时收纳目录。硬件 API 放在 `drivers`，进程级资源所有权放在 `library`。
Sentinel 不解析原始 CAN，也不持有 Camera/Recording 实现；SR501 电平采集和去抖属于 STM32。
Hawkeye 不访问 V4L2、Argus 或 Camera Driver，也不引入 OpenCV/ROS2；图像采集仍由 Camera Driver
独占。
`CameraCalibration` 可转换为平台无关 `CameraInfo`；只有可选 ROS2 adapter 才允许将其转换为
`sensor_msgs/CameraInfo`。默认构建不查找或链接 ROS2。
`CameraCalibration` 可转换为平台无关 `CameraInfo`；只有可选 ROS2 构建才允许将其转换为
`sensor_msgs/CameraInfo`。默认构建不查找或链接 ROS2。
