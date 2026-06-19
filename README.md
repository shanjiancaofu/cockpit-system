# Smart-Cockpit-System

`system` 是新的 Jetson 智能车机 / 座舱原型工程落点，也就是运行在 Jetson 上的车端客户端，依据
`../architecture_refined_v0.3.md` 初始化。

云端不放在本目录中：

- `../cloud-backend`：云端后端服务。
- `../cloud-frontend`：云端管理前端。

当前阶段的目标不是一次性把所有旧项目代码搬进来，而是先完成可继续演进的工程骨架：

- `common/config`：轻量运行配置读取，后续替换为 `yaml-cpp`。
- `common/logging`：统一日志入口，支持文件输出和简单滚动。
- `common/proto`：保留 gRPC/protobuf 接口草案。
- `services/vehicle-data-service`：车辆状态服务占位，当前输出 mock `VehicleState`。
- `services/cockpit-gateway-service`：网关聚合服务占位，当前保留服务边界。
- `services/cloud-uplink-service`：车云上报服务占位，参考车云 HTTP/proto 字段。
- `tools/can-simulator`：CAN 模拟器占位，后续接 SocketCAN/vcan0。
- `docs/reference_projects.md`：旧项目复用分析和新工程决策入口。
- `docs/project_scope_and_repo_strategy.md`：当前项目范围、是否拆库、可用性/可行性/可拓展性决策。

`apps/web-dashboard` 如果保留，只代表 Jetson 本地调试页面，不代表云端管理前端。

## 源文件后缀

本工程统一使用 `.cc` 作为 C++ 源文件后缀。`.cc` 和 `.cpp` 没有语言层面的优劣，
选择 `.cc` 是为了贴近当前参考的 zelos C++ 代码库和 protobuf/gRPC 工程习惯。

## 构建

```bash
bash scripts/build.sh
```

Windows 主机建议在 WSL2 发行版内执行：

```bash
cd /mnt/e/code/project/system
bash scripts/install_ubuntu_deps.sh   # only needed once on a fresh Ubuntu distro
bash scripts/build.sh
bash scripts/run_smoke.sh
```

## 运行

```bash
build/bin/vehicle-data-service --config configs/config.yaml --samples 3
build/bin/cockpit-gateway-service --config configs/config.yaml
build/bin/cloud-uplink-service --config configs/config.yaml --once
build/bin/can-simulator --config configs/config.yaml --samples 5
```

如果本机没有 Ninja，`scripts/build.sh` 会自动退回 CMake 默认生成器。

## 旧项目参考边界

- `../vehicle-system`：Qt Widgets 车机壳、日志滚动、进程启动模式，适合参考和改造。
- `../zelos/car_cloud_server`：车辆侧配置拉取、状态上报、数据库仓储分层，适合沉淀协议与云端流程。
- `../zelos/zcarcloud*` 和 `../zelos/safe_ota`：C++17、xmake、protobuf、YAML、信号退出、包安装经验，适合总结规则，不直接复制。
- `../车机项目`、`../VechicleSystem-main`、`../无人车`：硬件 demo 和历史资料，只作为驱动、设备树、Qt demo 参考。
