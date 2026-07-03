# cockpit-system

运行在 Jetson Orin / Linux 上的智能车机与座舱原型系统。

当前主仓库：

```text
/home/ffz/code/github/cockpit-system
```

项目采用一个主仓库、多个内部 CMake 模块的方式开发。当前先完成 Jetson 本机链路，
云端后端和云端管理前端仅保留为未来可选扩展，不在现阶段创建独立仓库。

## 当前能力

- C++17 + CMake + Ninja 构建链路。
- 统一配置、日志、服务生命周期和车辆状态模型。
- `CanFrame` 与 Linux SocketCAN RAII 封装。
- `VehicleCanCodec` 与 `vehicle-data-service --source mock|socketcan`。
- protobuf/gRPC C++ 自动生成与 VehicleState server-streaming 链路。
- `can-simulator --backend stdout|socketcan`。
- `vehicle-data-service` 提供 mock/SocketCAN 数据源和 gRPC 状态流。
- `cockpit-gateway-service` 订阅车辆状态，支持断线重连与消息去重。
- ROS 风格 `topic list/info/pub/echo/hz` 调试工具。
- `audio-service` 麦克风采集控制面和 `audio-probe --start/--stop/--status`。
- 本地能量 VAD、输入 dBFS、speech/silence 状态和运行指标。
- Mock ASR consumer、transcript 历史和 gRPC 文本事件流。
- 可选 whisper.cpp ASR adapter，默认关闭并保持 mock 构建无第三方模型依赖。
- V4L2 摄像头枚举、GStreamer 预览采集和 camera-service 控制面。
- `core/ipc` POSIX shared memory 与 camera shared-frame 双缓冲数据面。
- Qt/QML cockpit UI，支持车辆状态、相机预览和相机 start/stop 控制。
- 本地 Web dashboard 目录骨架。

尚未完成的真实传输包括：WebSocket dashboard 输出、MQTT 云端上报和真实 AI provider。

## 目录

```text
apps/       Qt/QML UI 与本地浏览器调试页面
core/       配置、日志、生命周期、事件队列、IPC 和基础工具
modules/    vehicle、can、audio、voice、camera 等平台无关领域能力
drivers/    SocketCAN、ALSA、V4L2 等 Linux/硬件适配层
proto/      protobuf 与 gRPC 接口契约
configs/    YAML 与 systemd 配置
docs/       架构、范围、参考审计和变更记录
services/   Jetson 本机常驻服务
tools/      CAN 模拟器、topic 等开发工具
tests/      smoke 与单元测试
```

## 构建与验证

```bash
cd /home/ffz/code/github/cockpit-system
bash scripts/build.sh
bash scripts/run_smoke.sh
```

启用提交前检查：

```bash
pre-commit install
pre-commit run
pre-commit run -a
pre-commit run clang-tidy --hook-stage manual -a
```

默认开发目录是 `build/x86_64-debug`（Jetson 上为 `build/arm64-debug`）。`clang-tidy`
依赖其中的 `compile_commands.json`，首次运行前先执行 `bash scripts/build.sh`。

摄像头设备需要当前用户属于 `video` 组。若 `/dev/video*` 存在但提示 Permission denied，执行
`newgrp video` 或重新登录 WSL/Jetson 终端后再试。

构建目录统一采用 `build/<目标架构>-<构建类型>`，例如 `build/x86_64-debug` 和
`build/arm64-release`。不同架构不能共用 CMake cache。

## 常用命令

```bash
build/x86_64-debug/bin/can-simulator --backend stdout --samples 3
build/x86_64-debug/bin/vehicle-data-service --config configs/config.yaml --samples 3
build/x86_64-debug/bin/cockpit-gateway-service --config configs/config.yaml --samples 3
build/x86_64-debug/bin/cockpit-ctl status --config configs/config.yaml
build/x86_64-debug/bin/camera-probe --list --config configs/config.yaml
build/x86_64-debug/bin/camera-service --config configs/config.yaml
build/x86_64-debug/bin/topic list --config configs/config.yaml
```

SocketCAN 模式需要已经启动的 `vcan0` 或真实 `can0`。

WSL/Ubuntu 虚拟 CAN 端到端验证：

```bash
bash scripts/run_vcan_smoke.sh
```

Qt6 车机界面联调（自动启动车辆服务、网关和 camera-service）：

```bash
bash scripts/run_cockpit_ui.sh
```

Release 打包与 Jetson 部署：

```bash
bash scripts/build.sh --arch x86_64 --type release
bash scripts/package.sh
```

部署说明见 [docs/deployment.md](docs/deployment.md)。

## 文档

文档入口见 [docs/README.md](docs/README.md)。

重要文档：

- [总体架构蓝图](docs/architecture_refined_v0.4.md)
- [当前架构快照](docs/architecture.md)
- [Jetson 部署](docs/deployment.md)
- [项目范围与仓库策略](docs/project_scope_and_repo_strategy.md)
- [模块化策略](docs/modularization_strategy.md)
- [实施状态](docs/implementation_status.md)
- [变更记录](docs/change_log.md)
- [旧代码审计](docs/reference_code_audit.md)

## 工程约定

- C++ 源文件统一使用 `.cc`。
- C++ namespace 统一使用 `cockpit::...`。
- 硬件模块先支持 mock，再接真实设备。
- 公开接口与私有实现需要在头文件中明确区分。
- 每批代码改动同步更新 `docs/change_log.md`。
