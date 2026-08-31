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
│   ├── cockpit_chassis_safety/          production safety core ROS2 adapter
│   └── cockpit_nav2_test_support/       fake odom/TF/scan/chassis/readiness
└── _output/
    ├── build/ros2/                      主项目可选 ROS2 CMake 构建
    └── ros2/{build,install,log}/        colcon 输出
```

主项目继续使用普通 CMake；`ros2/src` 使用 colcon/ament。默认 CI 和默认产品构建保持
`COCKPIT_ENABLE_ROS2=OFF`。官方 ROS 2/Nav2 deb 安装在 `/opt/ros/humble`，版本由
`scripts/setup/ros2/versions.sh` 按架构选择。

## 安装和构建

```bash
cd /home/ffz/code/github/cockpit-system

bash scripts/setup/ros2/install.sh
bash scripts/setup/ros2/verify.sh
bash scripts/ros2/configure.sh
bash scripts/ros2/build.sh
bash scripts/ros2/test.sh

source /opt/ros/humble/setup.bash
source _output/ros2/install/setup.bash
```

`scripts/ros2/configure.sh` 负责主项目 ROS adapter 和 clangd compile database；
`scripts/ros2/build.sh` 负责四个 cockpit ament 包，并在显式准备后构建固定 revision 的外部
`rplidar_ros`；全部 colcon 和外部 checkout 输出放入 `_output/ros2`。
安装脚本只使用 sudo 写入 apt key、ROS deb 和首次 `rosdep init` 的系统目录；源码、构建和运行过程不以
root 执行。
GitHub CI 使用 `COCKPIT_SKIP_ROSDEP_SETUP=1`，因为所有构建依赖已经由 pinned apt 清单显式安装，
避免 `rosdep update` 对 rosdistro/GitHub 临时网络状态形成无关门禁。

主 CTest 的 `chassis_safety_adapter_test` 直接验证 finite/representable、线速度/角速度限幅、slew、
250 ms watchdog、enable、authority、e-stop、peer、chassis fault、clock regression、reset 和 stop reason；
它与 full-process smoke 分开，避免只通过导航结果间接证明安全边界。

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

## CameraInfo 与可选 Rectify

ROS2 adapter 位于 `cockpit/library/bridge/ros2_camera_frame_adapter.*`。它接收统一
`CameraFrame` 与已经通过 Hawkeye loader 的 `CameraInfo`，输出 `image_raw` 和 `camera_info`；
可选 `image_rect` 在 adapter 第一次使用时初始化 `initUndistortRectifyMap()`，后续每帧只执行
`remap()`。三个输出共享同一个 source/receive-derived stamp、sequence 对应的 sample，不在
CameraInfo 发布时重新调用 `now()`。输入格式固定为 BGRx，尺寸必须匹配 verified calibration，
错误布局、尺寸或 K/D 直接 fail closed。

当前 adapter 仍是 ROS2 bridge 组件，不自动创建 publisher；后续 Camera runtime publisher
应复用该 adapter，保持 `image_raw.header.stamp == camera_info.header.stamp`。

`Ros2CameraPublisher` 已封装三个 publisher，并用 `SyntheticPreviewSource` 验证真实 ROS topic 收发；它只
接收统一 `CameraFrame`，不打开 V4L2/Argus。当前尚未把该对象装入 Jetson Navigator Camera runtime，
因此 VM 结果只代表 publisher 软件合同，不代表真机图像链路验收。

## C1 LiDAR 软件边界

`ros2/src/cockpit_lidar_bringup` 只保存官方 `rplidar_ros` 的 launch/config 合同：串口默认
`/dev/ttyUSB0`、460800、topic `/scan`、frame `base_scan`。它不包含 Slamtec SDK、不复制或修改
官方驱动。`scripts/setup/ros2/prepare-rplidar-ros.sh` 将官方 ROS2 branch 的精确 revision
`24cc9b6dea97e045bda1408eaa867ce730fd3fc3` 准备到 `_output/ros2/external-src`，随后由 colcon 一起构建；
该目录不提交。VM 中继续使用 `cockpit_nav2_test_support/fake_scan_node`，真实 C1 到位后仅替换 source，
下游 Nav2、Safety 和 Fake/SocketCAN sink 不变。当前 C1 标记为 VM SOFTWARE VERIFIED /
NOT HARDWARE VERIFIED。

`c1.launch.py` 只启动真实驱动，不拥有 FakeScan。`lidar_nav2.launch.py use_fake:=true` 只启用
Nav2 baseline 的单个 FakeScan；`use_fake:=false` 只启用 C1 并关闭 baseline FakeScan。可用
`scripts/tests/lidar-nav2-source-smoke.sh` 验证 fake 模式只有一个 `/scan` publisher。

- `fake_odometry_node`：订阅 `/cmd_vel_safe`，只在内存中积分二维位置并发布 `/odom`、`odom→base_link`。
- `fake_tf_node`：发布测试用 `map→odom` 和 `base_link→base_scan` 静态 TF。
- `fake_scan_node`：发布 bounded LaserScan，`scenario` 支持 `empty`、`front_wall`、`left_obstacle`、
  `right_obstacle`、`narrow_passage`、`dropout`、`nan`、`inf`、`invalid_range` 和 `stale`；仅用于
  costmap/Nav2 故障矩阵，不代表真实 C1。
- `cockpit_chassis_safety/chassis_safety_adapter`：产品安全核心的 ROS2 adapter，执行 finite、限幅、slew、
  enable/authority/e-stop/peer/fault 和 250 ms watchdog，发布 `/cmd_vel_safe` 与明确 stop reason；节点本身不访问 SocketCAN。
- `fake_chassis_sink`：消费 `/cmd_vel_safe`，记录命令数量、非零状态和 safety status，明确不存在硬件/CAN 输出。
- `nav2_readiness_probe`：有界确认关键 lifecycle node 为 ACTIVE 且 NavigateToPose action ready。
- `nav2_fault_control`：只在测试域开关 fake odometry，并确认最新 cmd_vel 已归零。

fake odometry/TF/scan/chassis/fault-control 只能存在于 `cockpit_nav2_test_support`，不得进入正式车辆 mode；
`cockpit_chassis_safety` 是产品候选安全层，但当前只完成 VM/fake 与隔离 vcan0 验证，真实车辆参数、真实 can0 和 SocketCAN 硬件发送仍未验收。

当前 minimal launch 的 provisional policy 是线速度 ±400 mm/s、角速度 ±1200 mrad/s、线/角加速度
400 mm/s² 与 1200 mrad/s²、命令 timeout 250 ms、输出周期 20 ms。它们只用于 VM/fake correctness，
不是 STM32/电机量产参数。任一 interlock 进入不安全状态都会清除缓存命令；恢复后必须收到新的有效
`/cmd_vel`，不会恢复 e-stop 前的旧速度。

差速底盘 Twist 合同固定为 `vx=linear.x + wz=angular.z`。六个分量必须全部 finite，且 `linear.y`、
`linear.z`、`angular.x`、`angular.y` 必须严格为零；任何不支持的非零轴或 NaN/Inf 都触发
`invalid_command + zero`。生产 peer 状态来自 `chassis_safety/peer_heartbeat` 脉冲并以 steady clock
执行 300 ms freshness；fault sample 来自 `chassis_safety/chassis_fault_sample`，缺失或超过 300 ms 在
peer 存活时按 fault 处理。`chassis_safety/test/*` Bool latch 只有显式
`allow_test_state_override=true` 的测试 launch 才启用，不能作为真车状态源。

真实接入时，peer/fault 状态不得由 Bool topic 维护：`ChassisClient` 从 SocketCAN 解码 0x200/0x240，
`ChassisCanSafetyStateSource` 将 heartbeat/fault freshness 转为 Safety state；0x180 motion 和 0x181
odometry 同时进入 `ChassisState`，保留各自 source timestamp/sequence。当前 VM 只用 vcan0，真实 can0
必须等 STM32 合同、heartbeat、fault、bus-off 和急停语义联调后开放。

VM 增加 `VcanChassisSafetyLoop`，且构造阶段硬拒绝 `can0`。它将同一隔离 vcan 总线上的
0x200/0x240/0x181 输入送入现有状态源和 Safety，再经现有 `SocketCanChassisSink` 输出 0x101；用于
验证 fault、heartbeat stale、command stale 和恢复行为，不是生产 SocketCAN runtime。

`ToRosChassisOdometry()` 使用 0x181 的 x/y/heading 和该帧自带的 linear/angular velocity 生成
`nav_msgs/Odometry`。STM32 `odometry_timestamp_ms` 是设备时钟，adapter 不会把它伪装成 Unix/ROS epoch；
`ChassisOdometryTimeMapper` 显式处理 uint32 wrap/reset 并建立 host realtime anchor，随后
`Ros2ChassisOdometryPublisher` 发布 `/odom`。该 anchor 是 host-estimated sample time，真机仍需测量
offset/drift；不能写成硬件同步已验证。

Camera+LiDAR 的统一时间语义见 [时间戳合同](时间戳合同.md)。Hawkeye 最小投影只接受 rectified image K、
显式 `T_camera_lidar` 和时间差预算，不拥有 LaserScan subscriber、同步队列或 SLAM/VIO。

## 参考项目审计

`/home/ffz/Documents/project_move/project_move/无人车` 仅作为只读资料。当前实现借鉴其 colcon workspace、
ament package 和外置参数组织；没有复制其中 ROS1 `move_base`、节点直开 V4L2/SocketCAN、键盘直控、
历史自定义控制消息或构建/install/log 产物。

## 后续真实接入

Jetson 到位后先运行只读环境检查，不要在 VM 中伪造这些结果：

```bash
bash scripts/setup/jetson-preflight.sh
```

该脚本只检查 aarch64、GCC/CMake/Ninja/Git/ROS2、CUDA 可见性和 `/dev/snd`、`/dev/video0`、`can0`
等节点，不安装软件、不修改权限、不启动服务。缺少 GPIO 节点只作为当前硬件选配项记录。

1. 用真实机器人尺寸、footprint、速度/加速度和传感器量程替换 upstream 测试参数。
2. 用真实 localization/SLAM 提供 `map→odom`，用底盘里程计提供 `odom→base_link` 和 `/odom`。
3. 接入真实 LaserScan/PointCloud 前固定 QoS、frame、时间同步和故障语义。
4. `/cmd_vel` 必须先经过独立安全适配、限幅、心跳和急停合同，再转换为待确认的 STM32 协议；当前
   fake adapter 仅用于验证限幅/watchdog，不代表生产底盘安全实现。
5. 在上述合同和硬件验证完成前，禁止 `/cmd_vel → CAN 0x101` 直连。
