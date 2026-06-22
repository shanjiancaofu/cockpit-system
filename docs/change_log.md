# 变更记录 / Change Log

本文记录 cockpit-system 的每批实现改动。后续记录统一包含变更内容、设计决定和验证结果。

This file records every implementation batch for cockpit-system. Future entries include
changes, design decisions, and verification results.

## 2026-06-22 - Qt UI 数据新鲜度 / Qt UI Data Freshness

### 变更内容 / Changed

- `VehicleStateModel` 新增独立 `fresh` 属性和 1.5 秒 stale timer。
- gRPC 已连接但车辆状态停止更新时，界面显示 STALE，不再把旧值标记为实时数据。
- transport 断开时立即停止 timer 并清除 fresh 状态。
- `ui_model` 拆为独立 target，新增 QtTest 覆盖初始、超时和断线行为。

### 验证结果 / Verification

- Qt UI 和 `vehicle_state_model_test` 编译通过。
- CTest 3/3 通过，fresh→stale 定时测试耗时约 80 ms。
- QML offscreen 加载无属性绑定错误，SIGTERM 后状态码 0。

## 2026-06-22 - Qt UI 一键联调 / Qt UI Demo Runner

### 变更内容 / Changed

- 新增 `scripts/run_cockpit_ui.sh`，一条命令启动车辆服务、网关和 Qt UI。
- 使用 gRPC topic discovery 探测网关就绪，不依赖固定启动延时。
- UI 退出或脚本收到信号后，统一停止并等待后台服务。
- 默认使用 mock 数据，可通过 `VEHICLE_SOURCE=socketcan` 切换 CAN 数据源。
- 网关上游 gRPC stream 增加停止监视，信号到达后主动取消阻塞式 `Read()`。

### 验证结果 / Verification

- offscreen 一键链路成功启动并连接 UI、gateway 和 vehicle-data-service。
- 单次 Ctrl+C 后 UI、gateway 和 vehicle-data-service 均完成清理，无残留进程。
- 网关在信号到达后约 20 ms 完成上游取消并退出，不再等待 10 秒 stream deadline。
- Codex 隔离网络中仍观察到 localhost gRPC reset；无代理对照未改善，需在普通 WSL
  终端继续确认宿主环境表现。

## 2026-06-22 - Qt UI 信号退出 / Qt UI Signal Shutdown

### 变更内容 / Changed

- `cockpit-ui` 增加 Qt event loop 与 `ServiceRuntime::ShouldStop()` 的生命周期桥接。
- SIGINT/SIGTERM 到达后由主线程调用 `QCoreApplication::quit()`。
- `aboutToQuit` 继续负责取消 gRPC context 并 join worker thread。

### 验证结果 / Verification

- Qt6 QML 页面已在 offscreen 平台成功加载。
- `cockpit-ui` 重新构建通过；单次 SIGTERM 后打印 stopped 并以状态码 0 退出。

## 2026-06-21 - Qt 6 车机界面基线 / Qt 6 Cockpit UI Baseline

### 变更内容 / Changed

- `cockpit-ui` 从 Qt 5.15 构建声明迁移到 Qt 6。
- QML import 移除 `2.15/1.15` 版本锁，使用 Qt 6 模块解析。
- Ubuntu 依赖替换为 Qt 6 base、declarative 和对应 QML runtime modules。
- UI 通过 C++ `GatewayClient` 订阅 CockpitEvent，通过 `VehicleStateModel` 更新 QML。

### 设计决定 / Design Decisions

- 新项目不保留 Qt5 fallback，减少双版本兼容成本。
- Ubuntu 22.04 的 Qt 6.2.4 只作为 WSL 编译下限。
- Jetson 发布环境固定 Qt 6.8 LTS 工具链，并通过 `CMAKE_PREFIX_PATH` 显式选择。
- Qt UI 保持可选 target，headless 服务构建不依赖图形栈。

### 验证结果 / Verification

- Ubuntu 22.04 软件源确认可提供 Qt 6.2.4 base、declarative 和 QML runtime modules。
- Qt 6.2 使用通用 `add_executable`、`AUTOMOC` 和 `AUTORCC`，不依赖较新 Qt 的
  `qt_standard_project_setup`。
- QML runtime 依赖补充 `QtQuick.Templates` 与 `QtQuick.Window` 模块。
- headless 默认构建和 CTest 2/2 通过，`BUILD_COCKPIT_UI=ON` 编译链接通过。
- offscreen 启动已进入 QML engine；补装新增的 Templates/Window runtime 后继续验证页面加载。

## 2026-06-21 - gRPC topic 发现 / gRPC Topic Discovery

### 变更内容 / Changed

- Gateway 协议新增 `TopicMetadata`、`ListTopics` 和 `GetTopicInfo`。
- 网关公开 `/vehicle/state` 的消息类型、来源和读写能力。
- `topic list/info --backend grpc` 可查询运行中网关，不再依赖文件 registry。
- 默认 smoke 新增 gRPC topic 发现和类型信息验证。

### 设计决定 / Design Decisions

- 当前元数据由网关显式维护，不提前引入动态注册中心。
- `/vehicle/state` 标记为可订阅、不可发布，避免调试工具伪造车辆状态。
- gRPC 连接和错误映射集中在 `TopicGrpcDiscovery`，命令文件只处理展示。

### 验证结果 / Verification

- proto 重新生成、增量构建和 CTest 2/2 通过。
- 完整 smoke 中 `topic list` 返回 `/vehicle/state`，`topic info` 返回类型、来源和读写能力。
- discovery、echo、hz 以及上游断线自动重连链路均验证通过。

## 2026-06-21 - 网关事件流与 topic gRPC / Gateway Events and topic gRPC

### 变更内容 / Changed

- `cockpit-gateway-service` 实现 `SubscribeCockpitEvents` server-streaming 服务。
- VehicleState 上游消息转换为 CockpitEvent，并以“最新值 + 版本号”方式转发。
- `topic echo` 和 `topic hz` 新增共享 gRPC 订阅后端，当前支持 `/vehicle/state`。
- 新增 `--backend grpc`、`--count` 和 `--max-hz` 参数。
- 默认 smoke 覆盖 vehicle → gateway → topic 的双段 gRPC 数据流。

### 设计决定 / Design Decisions

- 文件后端继续保留，用于离线调试；gRPC 后端用于运行中服务订阅。
- 网关和 topic 使用生成的 protobuf 类型，不增加无类型内部消息总线。
- 当前只公开已有 schema 的 `/vehicle/state`，未知 topic 明确返回错误。
- gRPC 服务和客户端使用显式 Start/Stream/Shutdown 生命周期，不隐藏后台线程。

### 验证结果 / Verification

- 增量构建通过，CTest 2/2 通过。
- `topic echo` 通过网关连续收到 3 条 VehicleState CockpitEvent。
- `topic hz` 通过网关测得约 4.963 Hz，与 200 ms mock 发布周期一致。
- 完整 CAN/stdout、双段 gRPC、cloud placeholder 和文件 topic smoke 通过。

## 2026-06-21 - zcarcloud 配置架构参考 / zcarcloud Configuration Reference

### 变更内容 / Changed

- 审计 `z-car-cloud.yaml`、`CarCloud::InitEnv`、`ConfigManager`、listener runtime 和各 target 依赖。
- 新增 `docs/configuration.md`，确定当前项目的配置分区、类型归属和启动校验规则。
- Ubuntu 依赖增加 `libyaml-cpp-dev`，用于替换当前两层标量解析器。
- 新增不可变 `SystemConfig` 与 system、paths、logging、services、hardware、features、tools
  类型化子配置。
- `config.yaml` 按职责重新分区，全部服务和工具移除 dotted string key 读取。
- 日志 `mirror_stderr`、gRPC stream timeout/retry 参数开始实际生效。
- 删除重复且未使用的 `configs/logging.yaml`。

### 设计决定 / Design Decisions

- 采用 zcarcloud 的配置驱动、类型化转换和显式组件生命周期。
- 不复制全局 ConfigManager 单例、Zoo service locator 或 zelos 平台专用依赖。
- gRPC、CAN、MQTT 等组件读取类型化子配置，不再长期保留 dotted string key。

### 验证结果 / Verification

- yaml-cpp 0.7.0 配置库编译通过。
- `system_config_test` 验证真实配置加载和非法 gRPC 地址路径提示。
- CTest 2/2 通过，完整 gRPC/CAN/topic smoke 通过。

## 2026-06-21 - VehicleState gRPC 数据流 / VehicleState gRPC Stream

### 变更内容 / Changed

- 新增 `proto/CMakeLists.txt`，通过 `protoc` 和 `grpc_cpp_plugin` 生成 `contracts` target。
- proto package 统一为 `cockpit.proto.*`，避免生成类型与领域模型同名冲突。
- `vehicle-data-service` 新增同步 server-streaming 服务，将最新 VehicleState 推送给订阅者。
- `cockpit-gateway-service` 新增 streaming 客户端、10 秒限时重连和时间戳去重。
- 默认 smoke 改为双进程 gRPC 集成测试，自动启动和停止 vehicle 服务。

### 设计决定 / Design Decisions

- 生成代码只进入 `build/proto/generated`，不提交到仓库。
- 服务端只保存最新状态和版本号，不无限缓存过期车辆状态。
- 本机 gRPC 使用 insecure credentials；远程接口不得直接复用该安全配置。
- Vehicle gRPC 默认只监听 `127.0.0.1:50050`，不暴露到 Jetson 外部网卡。
- 本机 channel 显式关闭 HTTP proxy，避免系统代理干扰 localhost 服务通信。

### 验证结果 / Verification

- protobuf 3.12.4、gRPC 1.30.2 代码生成和编译通过。
- `cockpit_smoke_test` 通过。
- gateway 通过 `127.0.0.1:50050` 连续收到 3 条 VehicleState，完整 smoke 通过。

## 2026-06-21 - gRPC 构建前置 / gRPC Build Prerequisites

### 变更内容 / Changed

- 检查 WSL2 当前工具链，确认尚未安装 `protoc` 和 gRPC C++ 开发库。
- 确认 Ubuntu 22.04 apt 提供 protobuf 3.12.4 与 gRPC 1.30.2。
- `scripts/install_ubuntu_deps.sh` 增加 protobuf 编译器、gRPC 插件和 C++ 开发包。

### 设计决定 / Design Decisions

- 当前阶段使用 Ubuntu 系统包，暂不引入 Conan、vcpkg 或源码编译 gRPC。
- 依赖安装完成后再提交并验证 proto 生成和 gRPC 业务代码，避免未编译代码进入主链路。

### 验证结果 / Verification

- 依赖已在 WSL2 安装，`protoc`、`grpc_cpp_plugin` 和 pkg-config 检查通过。

## 2026-06-21 - SocketCAN 车辆数据链路 / SocketCAN Vehicle Data Path

### 变更内容 / Changed

- 新增平台无关 `VehicleCanCodec`，定义可测试的原型 `0x123` VehicleState 帧。
- `vehicle-data-service` 支持 `--source mock|socketcan`，并将业务循环从 `main.cc` 拆出。
- SocketCAN 接收支持 poll 超时、空闲退出、无关帧忽略和非法帧告警。
- `can-simulator` 改为通过相同 codec 生成车辆状态帧。
- 新增 `scripts/setup_vcan.sh` 与 `scripts/run_vcan_smoke.sh`。
- Ubuntu 依赖增加 `can-utils`、`iproute2` 和 `kmod`。

### 设计决定 / Design Decisions

- CAN 字节映射属于 `modules/vehicle`，Linux socket 访问继续留在 `drivers/socketcan`。
- `0x123` 是 WSL/Jetson 原型测试协议，不冒充旧项目或真实车辆 DBC。
- 正式 DBC 到位后替换 codec，不改变 SocketCAN 驱动和服务生命周期。

### 验证结果 / Verification

- 标准 `build/` 增量构建完成，`cockpit_smoke_test` 通过。
- codec 往返、无关帧忽略和短帧拒绝均有测试覆盖。
- 默认 mock/stdout `scripts/run_smoke.sh` 完整通过。
- 在 WSL2 交互终端运行 `scripts/run_vcan_smoke.sh`，成功创建并启用 `vcan0`。
- `can-simulator` 通过 SocketCAN 发送 3 帧，`vehicle-data-service` 完整接收并解码为
  12.0、15.5、19.0 km/h，对应挡位与 SOC 也一致。

## 2026-06-20 - common 职责复核 / Common Responsibility Review

### 变更内容 / Changed

- 抽样检查 `znavigator`、`safe_ota`、`zcarcloud` 以及云端前后端工程的分层方式。
- 确认 zelos 中的 `common` 主要保存组件内部共享的状态、协议结构和抽象接口，
  并不是跨项目基础设施的固定目录。
- 当前仓库不恢复顶层 `common`；对应职责由 `proto` 和具体 `modules` 承担。
- 删除兼容用 `core` 聚合 target，服务和工具改为声明最小直接依赖。
- 修正旧项目审计中被误改的 `zcarcloud/common` 原始路径。

### 设计决定 / Design Decisions

- `core/` 只是基础设施分类目录，不再代表“一次链接全部基础库”的 target。
- 只有出现被多个同级模块共享、且无法归属现有领域的接口或状态类型时，才考虑
  `modules/common`；禁止用它收纳暂时不知道放哪里的代码。

### 验证结果 / Verification

- 标准 `build/` 完成 38/38 构建步骤。
- `cockpit_smoke_test` 通过。
- 完整 `scripts/run_smoke.sh` 链路通过。

## 2026-06-20 - 核心、模块与驱动分层 / Core, Module, and Driver Layers

### 变更内容 / Changed

- 将通用基础设施从 `common` 迁移到 `core`。
- 将 `vehicle` 和平台无关的 `CanFrame` 迁移到 `modules`。
- 将 Linux `SocketCan` 迁移到 `drivers/socketcan`，并新增独立 `socketcan` target。
- protobuf 契约迁移到顶层 `proto`，生成代码继续放在 `build` 目录。
- 顶层 CMake 仅聚合 `core`、`modules`、`drivers` 和产品目录。

### 设计决定 / Design Decisions

- `core` 只提供配置、日志、生命周期和基础工具，不包含领域模型或硬件访问。
- `modules` 保存平台无关的领域能力；`drivers` 保存 Linux 与硬件适配。
- `socketcan` 依赖 `can`，反向依赖禁止；服务和工具按需链接最小 target。
- 内核模块和设备树可放在对应 `drivers/<device>` 下，但不进入默认用户态构建。

### 验证结果 / Verification

- `core`、`modules`、`drivers` 分层在标准 `build/` 中配置和构建成功。
- 独立生成 `libcan.a` 和 `libsocketcan.a`，依赖传播正确。
- `cockpit_smoke_test` 与完整 smoke 链路通过。

## 2026-06-20 - common 构建模块化 / Common Build Modularization

> 本节记录迁移前的首次模块化；当前目录边界以上一节为准。

### 变更内容 / Changed

- 参考 `znavigator`，当时为配置、日志、生命周期、工具、车辆与 CAN 模块分别增加
  独立 `CMakeLists.txt`。
- 当时由统一聚合入口管理各子目录；该入口现已拆成 `core`、`modules` 和 `drivers`。
- 每个模块声明自己的直接依赖。
- `core` 改为 INTERFACE 兼容聚合 target，现有服务暂时无需整体改链接方式。

### 设计决定 / Design Decisions

- 新代码优先链接实际需要的最小 target。
- 不提前创建没有源码的 `audio`、`ai` 空 target。
- `core` 不聚合 `can`、`audio` 或 `ai` 等功能模块。

### 验证结果 / Verification

- 标准 `build/` 目录重新配置并完成 37/37 构建步骤。
- 成功生成 `libconfig.a`、`liblogging.a`、`libruntime.a`、`libutils.a`、
  `libvehicle.a` 和 `libcan.a`。
- `cockpit_smoke_test` 通过。
- 完整 `scripts/run_smoke.sh` 链路通过。

## 2026-06-20 - 文档收拢 / Documentation Consolidation

### 变更内容 / Changed

- 根 `README.md` 更新为当前仓库、已实现能力和可运行命令入口。
- `docs/README.md` 调整为文档导航和文档优先级说明。
- 纳入完整架构蓝图 `docs/architecture_refined_v0.3.md`。
- 架构蓝图中的项目名、安装路径和当前阶段说明统一为 cockpit-system。
- 项目名、仓库名和部署目录统一使用 `cockpit-system`。
- 明确当前只维护 `cockpit-system` 主仓库，云端前后端是未来可选拆分。
- 修正当前架构快照的蓝图链接，并统一 scope/modularization 中的仓库名和 CAN 状态。

### 验证结果 / Verification

- 文档链接和旧项目名通过文本检查。
- 清理旧 CMake cache 后，使用标准 `bash scripts/build.sh` 重新配置并构建成功。
- `cockpit_smoke_test` 通过。

## 2026-06-20 - SocketCAN 基础层 / SocketCAN Foundation

### 变更内容 / Changed

- 新增内部 CMake target：`can`。
- 新增 `CanFrame`，支持标准帧/扩展帧 ID、DLC、标志位校验和 candump 格式输出。
- 新增 `SocketCan`，支持 fd RAII、移动语义、接口绑定、发送、poll 超时和接收。
- `can-simulator` 新增 `--backend stdout|socketcan`。
- 当时新增 `can.simulator_backend` 配置（现为 `hardware.can.simulator_backend`），安全默认值为 `stdout`。
- `cockpit_smoke_test` 增加 CAN 帧测试。
- 参考并审计 `/home/ffz/code/project/无人车/can/can_ws/src/can_analyze`。
- 变更记录调整为中英双语格式。

### 设计决定 / Design Decisions

- Linux/SocketCAN 细节封装在 `SocketCan` 内部。
- 不复制 ROS2 依赖、全局 socket、分离线程和固定全局帧缓冲区。
- 没有 `vcan0` 或真实 CAN 硬件时，`stdout` 模式仍必须可运行。

### 验证结果 / Verification

- 使用 GCC 11.4.0 和 Ninja 在标准 `build/` 目录中构建成功。
- `cockpit_smoke_test` 通过。
- 默认 `stdout` CAN 后端下，完整 `scripts/run_smoke.sh` 链路通过。
- 本节记录时尚未配置 `vcan0`；该验证已于 2026-06-21 完成，见最新记录。
