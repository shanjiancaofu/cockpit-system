# 旧项目参考与复用分析

本文档对应 `architecture_refined_v0.4.md` 的参考项目约束，用于记录
`/home/ffz/code/project` 下旧项目对当前 `cockpit-system` 的影响。

更细的逐文件审计见 [reference_code_audit.md](reference_code_audit.md)。

当前仓库边界：`cockpit-system` 只作为 Jetson 车端系统。云端后端和前端尚未创建，等待出现
真实部署需求后再决定是否拆分。

## 结论摘要

| 旧项目 | 复用等级 | 对 `system` 的影响 |
|---|---|---|
| `vehicle-system` | 改造复用 | UI 迁移路径、Qt 日志滚动、运行时进程启动、资源目录组织 |
| `VechicleSystem-main` | 只参考 | 旧 Qt 工程形态，与 `vehicle-system` 重合度高，命名保留历史拼写 |
| `zelos/car_cloud_server` | 参考并抽取协议 | 车辆配置拉取、状态上报、在线状态、Repository 分层、HTTP API 文档 |
| `zelos/car_cloud_front_end` | 参考 | React/Vite 管理端、API mapper、测试方式、错误提示映射 |
| `zelos/zcarcloud`、`zelos/zcarcloud0527` | 参考并沉淀规则 | C++17、xmake、protobuf 生成、MQTT/TCP/TLS 目录划分、安装包规则 |
| `zelos/safe_ota` | 参考 | YAML 初始化、日志部署、服务启动顺序、HTTP/protobuf 请求封装 |
| `zelos/znavigator` | 参考 | Orin 平台构建参数、xmake 规则、包构建脚本 |
| `车机项目`、`VechicleSystem-main` | 只参考 | i.MX6U Qt demo、V4L2、传感器、驱动和设备树历史代码 |
| `无人车` | 只参考 | Jetson/Orin 设备树、相机、CAN、ROS/驱动资料 |

## 1. `vehicle-system`

观察到的能力：

- 顶层 qmake 工程构建 `VehicleSystem`、音乐、视频、天气等过渡应用。
- 新车机壳位于 `src/app`，采用 Qt Widgets、`QStackedWidget`、底部 tab。
- `src/service/runtime/AppProcessService` 用 `QProcess::startDetached` 启动旧应用。
- `src/libs/runtime/Logger` 通过 `qInstallMessageHandler` 写文件日志，支持 `*.log.1`
  简单滚动，并支持 `VEHICLE_SYSTEM_LOG_DIR` 和 `VEHICLE_SYSTEM_LOG_TO_STDERR`。
- `legacy/imx6u` 明确只作为旧板卡参考，不应直接进入 Jetson 主线。

新工程决策：

- UI 初期可以参考 Widgets shell 的页面组织，但新工程保留 `cockpit/apps/cockpit-ui` QML 方向。
- 日志策略采用“每服务独立日志文件 + 滚动 + 可镜像 stderr”，已落到
  `cockpit/core/logging`。
- 旧应用启动模式只作为迁移桥，不作为新架构长期方案。
- 硬件访问必须从 UI 中剥离到 service/platform 层。

## 1.1 `VechicleSystem-main`

该目录是旧 Qt 车机工程快照，目录名保留了历史拼写。它的主要价值是帮助确认
`vehicle-system` 中已经整理过的迁移方向，当前不作为新工程直接复制来源。

新工程决策：

- 优先参考整理后的 `vehicle-system`，避免同时吸收两个相近 Qt 工程导致目录和资源重复。
- 历史 UI 资源如果需要复用，应先进入 `resources/` 或设计资产目录，再由新 UI 统一引用。

## 2. `zelos/car_cloud_server`

观察到的能力：

- Go 服务结构清晰：`cmd/server`、`internal/config`、`internal/httpapi`、
  `internal/service`、`internal/storage/db`、`internal/storage/file`。
- 配置加载先给默认值，再用文件覆盖。
- 支持 memory/sqlite/mysql 三种仓储后端。
- 车辆侧主流程已经文档化：平台配置 pull、平台配置下载、车辆配置 pull、车辆状态上报。
- `vehicle_cloud_api.proto` 给出了车辆状态上报、配置拉取和 SSL 配置字段。

新工程决策：

- `cloud.proto` 先吸收 `vehicle_id`、`cloud_enabled`、`report_ok`、
  `timestamp_ms`、`abnormal_reason` 等稳定字段。
- `cloud-uplink-service` 第一阶段只做本地占位，后续再接 MQTT 或 HTTP。
- 配置读取采用“默认值 + 文件覆盖 + 明确错误”的模式。
- 本地服务内部 proto 与云端 proto 分开演进。

## 3. `zelos/zcarcloud` 和 `zelos/zcarcloud0527`

观察到的能力：

- C++17 主工程使用 xmake，开启 `-Wall -Wextra -Werror`。
- 通过 xmake 规则生成 protobuf C++ 文件，并将 generated include 目录公开给依赖方。
- 目录按 `common`、`client`、`listener`、`transfer`、`table`、`runtime`、
  `service` 等模块拆分。
- 安装包规则会安装头文件、proto、yaml 配置和静态库。
- `carcloud/main.cc` 有清晰的信号退出模型：安装 SIGINT/SIGTERM，Start，等待退出，
  Stop，恢复 signal 默认处理。

新工程决策：

- 主工程仍使用 CMake + Ninja；xmake 经验沉淀为 proto 生成、包安装和编译选项规则。
- 服务入口后续统一成 `LoadConfig -> InitLogger -> InitService -> Start -> WaitSignal -> Stop`。
- `transfer/mqtt`、`transfer/tcp`、`transfer/tls` 的拆分方式适合后续 `cloud-uplink-service`。

## 4. `zelos/safe_ota`

观察到的能力：

- `main.cc` 使用命令行 mode 参数选择具体业务实现。
- 启动阶段先加载 YAML，再部署日志配置，再创建业务对象。
- 对目录创建、日志初始化、业务工厂创建都有明确错误日志。
- `xmake.lua` 展示了二进制安装后移动到 `bin/tools/safe_ota` 的模式。

新工程决策：

- YAML 和日志是所有服务共同基础设施，已优先创建 `cockpit/core/config` 和 `cockpit/core/logging`。
- 后续业务复杂后可以引入 factory，但当前阶段不提前抽象。
- 工具类二进制统一放在 `tools/`，安装时进入 `bin/tools/...`。

## 5. 旧 Qt / i.MX6U / 无人车资料

观察到的能力：

- 包含 V4L2 摄像头、OpenCV 摄像头、DHT11、SR04、SR501、SG90、LED、
  AP3216C、ICM20608、设备树和内核模块材料。
- 大量路径、设备节点和内核版本绑定 i.MX6U，不适合直接进入 Jetson 主线。
- `无人车` 下有 Orin/Jetson 设备树、相机、CAN、ROS、驱动包等资料。

新工程决策：

- 先用用户态接口和 mock 跑通链路，再按 Jetson 适配具体硬件。
- 摄像头第一阶段走 USB + V4L2/GStreamer，CSI/libargus 放到后续。
- 驱动和设备树资料只进入 `docs/` 或 `legacy/` 说明，不进入默认构建。

## 回填到架构文档的建议

1. 第 3 节技术选型：主工程 CMake 保持不变；xmake 只作为旧项目参考和 demo 工具。
2. 第 9 节 protobuf：保留内部 proto 和云端 proto 分离；云端字段参考 `vehicle_cloud_api.proto`。
3. 第 10 节 YAML：采用默认值覆盖模型；服务端口、日志目录、CAN 接口、MQTT broker
   已进入 `configs/config.yaml`。
4. 第 15 节工程规范：加入信号退出模型、每服务独立日志、安装 cockpit/proto/config 的规则。
5. 第 17 和 18 节：第一阶段仍建议 `can-simulator -> vehicle-data-service -> gateway`，
   Qt/QML、WebSocket、MQTT 按依赖准备情况分阶段接入。
