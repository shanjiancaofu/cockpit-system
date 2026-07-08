# 参考代码审计

本文记录旧项目和 zelos 代码对 `cockpit-system` 的可复用经验。参考的目标是理解边界和工程方法，
不是复制目录或历史包袱。

## znavigator

参考路径：`/home/ffz/code/project/zelos/znavigator`。

值得借鉴：

- 薄 executable 入口。
- 独立模块 target 和明确依赖。
- process、protocol、operator、配置和生命周期分离。
- REST/IPC 作为控制与运行管理手段，而不是所有数据都走 RPC。
- 动态库加载采用 RAII。

当前不照搬：

- xmake/blade 历史兼容结构。
- `build64_release` 等软链接。
- 大量子仓依赖锁。
- 配置加密和复杂白名单。
- Breakpad symbol 发布流程。
- 尚无稳定 ABI 时的 `dlopen` 插件系统。

结论：采用它的模块化和 Runtime 思路，继续使用本项目现有 CMake、显式装配和类型化接口。

## zcarcloud

参考路径：`/home/ffz/code/project/zelos/zcarcloud`。

值得借鉴：

- 配置按 system、service、transport、hardware 分组。
- gRPC/MQTT endpoint 集中配置。
- 模块只读取自己的配置 section。
- transport、业务和进程入口分离。

当前不照搬完整云平台、车队管理、复杂认证和多环境部署。

## 旧 CAN 项目

参考路径：`/home/ffz/code/project/无人车/can/can_ws`。

可复用经验：

- SocketCAN 的 socket、bind、poll/read/write 基本流程。
- CAN frame 和业务解码分层。
- 设备关闭和错误恢复必须由 RAII 管理。

需要避免：

- 全局 socket 和全局缓冲区。
- 固定线程无法停止。
- driver 层直接写业务状态。
- 未校验 DLC、frame ID 和系统调用结果。
- 把 ROS2 依赖带入当前轻量车机项目。

因此当前实现拆为 `cockpit/modules/can`、`cockpit/drivers/socketcan` 和 `cockpit/modules/vehicle`。

## 旧车机/媒体项目

参考过 `VechicleSystem-main/service/mediad/CameraDevice.*` 等摄像头实现。

可复用经验：

- V4L2 负责设备和 capability。
- GStreamer 负责格式转换和 pipeline。
- UI 不直接操作摄像头 fd。
- preview、recording 和 AI consumer 应分开。

当前 camera-service 只负责预览所有权和共享内存输出；研发录像和录包未来进入独立 recording
边界。

## ALSA 与语音项目

采用的工程原则：

- PCM device 使用 RAII。
- 格式模型与 ALSA 句柄分离。
- 采集线程不能被 VAD/ASR 阻塞。
- XRUN、timeout、device error 使用明确状态和指标。
- WAV 工具用于可重复测试，不作为实时 pipeline。

当前链路使用 20 ms `AudioFrame`、SPSC ring、Energy VAD、SpeechSegmenter 和可替换 ASR/TTS
接口。

## 参考原则总结

1. 复制设计思想，不复制项目规模。
2. 复用稳定算法和协议知识，不复用全局状态和隐式依赖。
3. 新抽象必须有当前调用方。
4. 车端实时数据与控制面分离。
5. 任何旧代码进入主项目前都必须满足 RAII、可停止、可测试和错误可观测。
