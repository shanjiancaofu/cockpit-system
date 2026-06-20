# 变更记录 / Change Log

本文记录 cockpit-system 的每批实现改动。后续记录统一包含变更内容、设计决定和验证结果。

This file records every implementation batch for cockpit-system. Future entries include
changes, design decisions, and verification results.

## 2026-06-20 - common 构建模块化 / Common Build Modularization

### 变更内容 / Changed

- 参考 `znavigator`，为 `common/config`、`logging`、`runtime`、`utils`、`vehicle`、
  `can` 分别增加独立 `CMakeLists.txt`。
- `common/CMakeLists.txt` 改为模块规则和 `add_subdirectory()` 聚合入口。
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
- 新增 `can.simulator_backend` 配置，安全默认值为 `stdout`。
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
- 当前主机没有配置 `vcan0`，SocketCAN 运行时验证暂待完成。
