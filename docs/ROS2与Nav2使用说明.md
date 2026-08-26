# ROS 2 Humble 与 Nav2 使用说明

更新时间：2026-08-26。

## 当前结论

cockpit-system 保持单 Git 仓库，ROS 2 自有代码放在 `ros2/src`，不复制或 fork Nav2 官方源码。
Ubuntu 22.04 x86_64 已完成以下功能基线：

```text
bridge-ctl
  → BridgeControl gRPC
  → BridgeService
  → Ros2Nav2Provider
  → Nav2 NavigateToPose
  → planner/controller/BT Navigator
  → /cmd_vel
  → bounded safety adapter (`/cmd_vel_safe`)
  → test-only sink + planar odometry simulation
```

map server、controller server、planner server 和 BT Navigator 均实际进入 ACTIVE；目标成功、反馈
pose/timestamp、最终取消和非零 `cmd_vel` 计数已经由全进程 smoke 验证。correctness 矩阵还覆盖
越界不可达目标、odometry/TF stale、有界失败、恢复、BT Navigator 进程退出、Bridge 断线与整栈重启；
每个 success/cancel/failure/timeout 终态都确认最新 `/cmd_vel` 为零。该结果不连接 CAN、电机或真实
机器人，不能替代 Jetson、定位、雷达、底盘和现场安全验收。

Goal acknowledgement timeout 进入内部 uncertain/pending 保护：迟到的 Nav2 ACK 由 action callback
自动取消，不依赖外部 `GetStatus()` 轮询；若自动取消被拒绝，保护保持到旧 goal 自身报告 terminal，
期间禁止新 goal。

## 目录和构建边界

```text
cockpit-system/
├── cockpit/modules/bridge/             平台无关 NavigationGoal/Pose/Status
├── cockpit/library/bridge/             Navigator 生命周期、gRPC 和 ROS adapter
├── ros2/src/
│   ├── cockpit_nav2_bringup/           launch、测试地图、参数所有权
│   └── cockpit_nav2_test_support/       fake odom/TF/scan/cmd_vel sink/readiness
└── _output/
    ├── build/ros2/                      主项目可选 ROS2 CMake 构建
    └── ros2/{build,install,log}/        colcon 输出
```

主项目继续使用普通 CMake；`ros2/src` 使用 colcon/ament。默认 CI 和默认产品构建保持
`COCKPIT_ENABLE_ROS2=OFF`。官方 ROS 2/Nav2 deb 安装在 `/opt/ros/humble`，版本由
`scripts/ros2-humble-pins.env` 固定。

## 安装和构建

```bash
cd /home/ffz/code/github/cockpit-system

bash scripts/setup-ros2-humble-nav2.sh
bash scripts/verify-ros2-humble.sh
bash scripts/configure-ros2-dev.sh
bash scripts/build-ros2-workspace.sh
bash scripts/test-ros2-workspace.sh

source /opt/ros/humble/setup.bash
source _output/ros2/install/setup.bash
```

`configure-ros2-dev.sh` 负责主项目 ROS adapter 和 clangd compile database；
`build-ros2-workspace.sh` 负责两个 ament 包，并把全部 colcon 输出放入 `_output/ros2`。
安装脚本只使用 sudo 写入 apt key、ROS deb 和首次 `rosdep init` 的系统目录；源码、构建和运行过程不以
root 执行。

## 运行和验证

只启动 Nav2 minimal baseline：

```bash
source /opt/ros/humble/setup.bash
source _output/ros2/install/setup.bash
ros2 launch cockpit_nav2_bringup minimal_nav2.launch.py
```

完整 Bridge + 官方 Nav2 smoke：

```bash
BUILD_DIR=_output/build/ros2 \
ROS2_INSTALL_DIR=_output/ros2/install \
  bash scripts/tests/nav2-minimal-bridge-smoke.sh
```

较小的自建 fake action server smoke 仍保留，用来隔离检查 Bridge action client 的断线、重启和取消
语义：

```bash
BUILD_DIR=_output/build/ros2 bash scripts/tests/ros2-nav2-bridge-smoke.sh
```

## 测试支持节点

- `fake_odometry_node`：订阅 `/cmd_vel_safe`，只在内存中积分二维位置并发布 `/odom`、`odom→base_link`。
- `fake_tf_node`：发布测试用 `map→odom` 和 `base_link→base_scan` 静态 TF。
- `fake_scan_node`：发布无障碍物的 bounded LaserScan。
- `fake_cmd_vel_safety_adapter`：将 Nav2 `/cmd_vel` 限幅到线速度 ±0.4 m/s、角速度 ±1.2 rad/s；命令
  超过 250 ms 未刷新时输出零，并且只发布到测试用 `/cmd_vel_safe`。
- `fake_cmd_vel_sink`：只统计 `/cmd_vel_safe`，明确不存在硬件/CAN 输出。
- `nav2_readiness_probe`：有界确认关键 lifecycle node 为 ACTIVE 且 NavigateToPose action ready。
- `nav2_fault_control`：只在测试域开关 fake odometry，并确认最新 cmd_vel 已归零。

这些节点只能存在于 `cockpit_nav2_test_support`，不得进入正式车辆 mode。

## 参考项目审计

`/home/ffz/Documents/project_move/project_move/无人车` 仅作为只读资料。当前实现借鉴其 colcon workspace、
ament package 和外置参数组织；没有复制其中 ROS1 `move_base`、节点直开 V4L2/SocketCAN、键盘直控、
历史自定义控制消息或构建/install/log 产物。

## 后续真实接入

1. 用真实机器人尺寸、footprint、速度/加速度和传感器量程替换 upstream 测试参数。
2. 用真实 localization/SLAM 提供 `map→odom`，用底盘里程计提供 `odom→base_link` 和 `/odom`。
3. 接入真实 LaserScan/PointCloud 前固定 QoS、frame、时间同步和故障语义。
4. `/cmd_vel` 必须先经过独立安全适配、限幅、心跳和急停合同，再转换为待确认的 STM32 协议；当前
   fake adapter 仅用于验证限幅/watchdog，不代表生产底盘安全实现。
5. 在上述合同和硬件验证完成前，禁止 `/cmd_vel → CAN 0x101` 直连。
