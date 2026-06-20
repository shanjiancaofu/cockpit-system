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
- `can-simulator --backend stdout|socketcan`。
- `vehicle-data-service` mock 车辆状态输出。
- `cockpit-gateway-service`、`cloud-uplink-service` 服务边界。
- ROS 风格 `topic list/info/pub/echo/hz` 调试工具。
- Qt/QML cockpit UI 和本地 Web dashboard 目录骨架。

尚未完成的真实传输包括：vehicle-data-service SocketCAN 接收、gRPC、WebSocket 和 MQTT。

## 目录

```text
apps/       Qt/QML UI 与本地浏览器调试页面
common/     core、can、proto 等内部模块
configs/    YAML 与 systemd 配置
docs/       架构、范围、参考审计和变更记录
services/   Jetson 本机常驻服务
tools/      CAN 模拟器、topic 等开发工具
tests/      smoke 与单元测试
```

## 构建与验证

```bash
cd /home/ffz/code/github/cockpit-system
rm -rf build
bash scripts/build.sh
bash scripts/run_smoke.sh
```

从旧目录复制过来的 `build/` 可能保存旧 CMake 路径。遇到 cache path mismatch 时删除
`build/`，然后重新执行标准构建脚本。

## 常用命令

```bash
build/bin/can-simulator --backend stdout --samples 3
build/bin/can-simulator --backend socketcan --samples 3
build/bin/vehicle-data-service --config configs/config.yaml --samples 3
build/bin/cockpit-gateway-service --config configs/config.yaml
build/bin/cloud-uplink-service --config configs/config.yaml --once
build/bin/topic list --config configs/config.yaml
build/bin/topic echo /dev/smoke --tail 1 --config configs/config.yaml
build/bin/topic hz /dev/smoke --window 100 --config configs/config.yaml
```

SocketCAN 模式需要已经启动的 `vcan0` 或真实 `can0`。

## 文档

文档入口见 [docs/README.md](docs/README.md)。

重要文档：

- [总体架构蓝图](docs/architecture_refined_v0.3.md)
- [当前架构快照](docs/architecture.md)
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
