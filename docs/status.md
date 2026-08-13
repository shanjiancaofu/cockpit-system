# 实现状态

更新时间：2026-08-13。

本文只记录代码已经落地到什么程度、入口在哪里、如何验证、下一步补什么。
项目推进顺序见 [roadmap.md](roadmap.md)，架构解释见 [architecture.md](architecture.md)。

## 状态分级

| 状态 | 含义 |
|---|---|
| 已落地 | 已有代码、构建通过，并进入 smoke 或单元测试 |
| 可用原型 | 主链路可跑，但还缺真实硬件、真实性能或异常场景验证 |
| 部分完成 | 有基础模块或接口，主链路尚未闭环 |
| 待实现 | 只有规划或占位，没有可用代码 |

## 模块状态表

| 模块 | 当前状态 | 主要入口 | 验证方式 | 下一步 |
|---|---|---|---|---|
| 工程基础 | 已落地 | `CMakeLists.txt`、`cmake/`、`scripts/`、`.github/workflows/ci.yml` | GCC Debug/Release、ASan/UBSan、Camera TSan、CTest、smoke、clang-tidy 和 pre-commit | 按失败证据维护门禁，不为假想场景扩展脚本 |
| 配置与日志 | 可用原型 | `cockpit/core/config`、`cockpit/core/logging` | 活动字段真实消费、严格未知字段、周期刷盘、时间切分、按模块保留上限和资源基线 | Jetson 长运行验证 |
| Navigator 动态运行时 | 可用原型 | `cockpit/navigator`、`cockpit/library` | mode、有界 IPC、重复崩溃恢复、RSS/线程/FD/目录占用基线及 Jetson systemd 重启、冷启动 | Jetson 实机长稳验证 |
| MessageBus | 已落地 | `cockpit/core/event` | `message_bus_test` | 仅在出现真实低频事件 consumer 时接入，不扩成跨进程总线 |
| IPC shared memory | 已落地 | `cockpit/core/ipc` | camera shared memory 崩溃恢复、帧边界测试和 UI 联调 | Jetson 权限、进程并发启动和长稳验证 |
| 车辆与 CAN | 可用原型 | `cockpit/library/driver/vehicle`、`cockpit/drivers/socketcan`、`cockpit/modules/vehicle` | `vcan0`、Jetson `mttcan can0` 内部闭环、vcan smoke、Navigator smoke | 接外置 CAN 收发器、真实总线和正式 DBC/车辆信号定义 |
| Transfer 与 topic | 已落地 | `cockpit/library/transfer`、`tools/topic` | transport、更新周期、可用状态、错误原因、gRPC test、Navigator smoke | 接入真实新数据源时扩展 metadata |
| 音频采集 | 可用原型 | `cockpit/library/driver/audio`、`cockpit/modules/audio`、`cockpit/drivers/alsa` | audio tests、null/ALSA smoke、Navigator smoke | Jetson 麦克风、增益、延迟和声学标定 |
| 语音识别 | 部分完成 | `agent/speech/asr`、`agent/speech/pipeline` | mock ASR、分段和 pipeline tests | 在 Agent 产品构建中接入并对比真实 ASR |
| 语音交互/Agent | 可用原型 | `agent/`、`cockpit/navigator/library/agent` | 单会话、显式状态转换、主动中断、队列丢弃、provider 超时/失败恢复和 voice smoke | 阶段 6 deadline/播放回执/FOLLOW_UP，再接 KWS 和真实 provider |
| HMI 动作桥接 | 部分完成 | `agent/hmi`、`cockpit/apps/cockpit-ui/hmi_control.*` | `open_camera` Qt 主线程切页、媒体未接入失败路径、Navigator smoke | 媒体责任边界明确后接 Qt/Android 播放器 |
| 语音播放 | 可用原型 | `agent/speech/tts`、`agent/audio`、`cockpit/library/driver/audio` | mock TTS 测试音、PCM RPC、异步播放和有界停止测试 | 接真实 TTS provider、播放完成回执和扬声器标定 |
| 相机采集 | 可用原型 | `cockpit/library/driver/camera`、`cockpit/drivers/v4l2`、`cockpit/modules/camera` | 合成故障测试、共享内存恢复、Navigator smoke、IMX219 Argus/NVMM 实拍、no-frames 恢复及正式部署 7 分钟短稳 | Jetson CSI 小时级长稳测试 |
| UI 健康总览 | 可用原型 | `cockpit/core/health`、`cockpit/apps/cockpit-ui/health`、QML Dashboard/Diagnostics | health/model tests、Qt offscreen、smoke | Jetson 图形栈和长运行状态可见性验证 |
| 相机 UI | 可用原型 | `cockpit/apps/cockpit-ui/camera` | Qt offscreen、Jetson Xorg 实窗、CSI 自动映射 Argus URI、IMX219 预览与 JPEG 拍照 | 补齐相机故障分类可视化和长稳验证 |
| 研发录包 | 可用原型 | `cockpit/library/recording`、`cockpit/modules/recording`、`recording-ctl` | recording tests、Navigator development mode、时间线、完整性和聚合报告 | 按长稳规模验证报告限制与生成耗时 |
| 诊断 CLI | 已落地 | `tools/diagnostics`、`cockpit-ctl`、`camera-ctl`、`voice-ctl`、`audio-probe` | `cli_output_test`、在线 JSON smoke、退出码检查 | 按实际自动化需求扩展其他命令 |
| 诊断快照 | 已落地 | `tools/cockpit-ctl/snapshot_command.*`、`scripts/tests/navigator-stability.sh` | 离线/在线快照、失败自动留证、保留上限和长运行目录占用 | Jetson 故障现场验证 |
| 部署脚本 | 可用原型 | `scripts/package.sh`、`deploy/` | Release 双版本、checksum、临时矩阵及 Jetson `/cockpit-system` 安装、systemd health/restart/rollback/cold boot | Jetson 异常掉电和 systemd 长运行验证 |
| 云端上传 | 待实现 | `cockpit/library/carupload` ABI 占位 | cloud mode 只验证模块生命周期，不建立 broker 连接 | 等 MQTT topic、设备身份、TLS、重试和后端协议明确后实现 |
| HMI 动态模块 | 可用原型、默认关闭 | `cockpit/library/hmi`、`cockpit/apps/cockpit-ui` | Qt offscreen、UI/HMI 双向崩溃恢复测试、显式 ui mode | Jetson 图形栈、触摸和长稳通过后再评估开机启动 |
| Debugger/Calibration/Watchdog | 待实现 | 对应 `cockpit/library/*` ABI 骨架 | 骨架拒绝启动 | 远程触发或持续采样需求明确后实现 debugger；其余等待硬件条件 |
| WebSocket/Web UI | 待实现 | 无 | 无 | 车端链路稳定后再决定是否做 |
| SQLite 状态索引 | 待实现 | 无 | 无 | 录包和状态查询需求明确后再加 |
| Upgrade/Safe OTA | 可用原型 | `cockpit/library/upgrader`、`tools/safe-ota` | Ed25519、anti-rollback、事务 fsync、四阶段恢复、完整进程树切换、卡死子进程清理、健康检查和回滚 | 实机拔电、密钥轮换/吊销、后端发布授权和车辆安全条件 |
| 崩溃收集 | 已落地 | `cockpit/navigator/diagnostics`、`cockpit/navigator/process` | JSON 合法性、信号故障注入、成功重启、重启上限、进程组回收、干净关闭和最多 20 份保留测试 | 后期按隐私和后端合同决定是否上传；当前不抓 core |

## 当前验证基线

开发机基线为 WSL2 Ubuntu 22.04；实机基线为 Jetson Orin Nano Super、L4T R36.4.3、
GCC 11.4、CMake 3.22 和 Qt 6.2。

```bash
bash scripts/build.sh
bash scripts/build.sh --type release
bash scripts/tests/smoke.sh
bash scripts/tests/wsl-matrix.sh
pre-commit run --all-files
```

当前 WSL 语音重构基线：独立 Debug/Release 构建均为 48/48 CTest，ASan/UBSan 聚焦测试 4/4，
pre-commit 全部通过。最近 Jetson 快照为 ARM64 Debug/Release 46/46 CTest 和 Navigator 单入口完整
smoke 通过；该数字是当时测试清单，不与当前 WSL 测试总数强行对齐。语音测试覆盖连续命令顺序、
主动中断当前 action、丢弃排队 transcript、provider 超时和失败后继续服务。Navigator 测试覆盖有界 IPC、模式切换、
`cockpit-ctl runtime`、显式重启、整组 reload、真实 development 模块健康采样、camera driver 崩溃恢复、
HMI/UI 双向崩溃恢复、重启限制和 ABI 不兼容拒绝。safe-ota 测试覆盖候选确认、SHA256、独立安装、
真实 upgrade mode、prepared 切换前后、activated、confirmed 重启恢复、健康检查和失败回滚。WSL 矩阵
覆盖配置边界、Release 双版本、包篡改
拒绝、临时安装、配置保留、mode health、gateway 断流恢复、recording 不可达、队列满、shared-memory
writer 重启、离线诊断快照和 rollback，并输出 `_output/runtime/reports/wsl-matrix.json`。诊断快照同时在
在线 smoke 中验证 Runtime 状态，日志按文件数和字节数截断，且不复制可能包含凭据的完整配置文件。
当前 `clang-tidy` 的 WarningsAsErrors 阻断基线通过；其余第三方头文件和非阻断告警不在本批做无关清理。

WSL-R3 的 normal/development 30 秒基线各完成 3 次 camera driver 崩溃恢复，资源样本均无缺失。
normal RSS 增加 604 KiB、FD 从 89 降至 86；development RSS 增加 928 KiB、FD 从 109 降至 106。
两次运行的录包、报告和快照占用不增长，日志分别增加约 44 KiB 和 39 KiB。

WSL-R4 曾使用 whisper.cpp `6fc7c33b`、`ggml-small.bin` 和 16 kHz mono JFK WAV 完成独立 GCC
Release 对照测试。输出文本正确，耗时 4.39 秒，CPU 393%，峰值 RSS 649232 KiB；该实现已退出当前
产品构建。

这条基线表示 WSL 原型具备自动验证证据，不表示真实 TTS、HMI 媒体控制、云端上传、Jetson 硬件或
量产升级已经完成。

## 真机证据边界

已验证：IMX219 Argus/NVMM 实际采集、Qt 预览、JPEG 拍照、两分钟 4146 帧和正式 systemd
7 分 33 秒 13599 帧零丢帧，以及 no-frames 故障恢复。

仍未验证：

- Jetson systemd 小时级长运行、异常掉电和全部硬件权限组合。
- 真实车辆 CAN、GPIO、I2C 等外设。
- 真实麦克风、扬声器、AEC 和噪声环境。
- IMX219 小时级预览稳定性。
- 真实 ASR、USB 麦克风、车内噪声、AEC、功耗和长运行性能。
- MQTT/TLS/鉴权、量产 OTA、崩溃上传和云端平台。
