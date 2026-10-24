# ros

职责：ROS2/Nav2 集成、Camera 与底盘数据消息适配。

- `bridge/`：原 Bridge 服务、provider 与 fake provider。
- `bridge/runtime/`：现有 gRPC 服务、ROS2 publisher、Nav2 provider 和模块入口。
- `src/`：colcon/ament 包，保留各包的 package.xml、CMakeLists、launch/config 与测试节点。
- `exchange/proto/bridge.proto`：共享协议，内容不随源码归属改变。

沿用现有 `bridge` target、Navigator 运行标识、动态库名称、Socket 和配置，保持 ROS2 可选构建开关。
模块不直接持有 Camera/CAN 设备，设备采集与安全约束仍归 driver、shield。
不另增进程。ROS2 源码和工作空间统一归本模块，根目录不再保留 ros2/。

```text
cockpit/ros/
├── src/
│   ├── cockpit_chassis_safety/
│   ├── cockpit_lidar_bringup/
│   ├── cockpit_nav2_bringup/
│   └── cockpit_nav2_test_support/
├── bridge/
├── CMakeLists.txt
├── compile_commands.json   # 构建生成的链接，不入库
└── README.md
```

`bridge/` 由主项目 CMake 构建，`src/` 继续使用 colcon/ament。
官方 ROS2 Humble 与 Nav2 包仍位于 /opt/ros/humble；官方 rplidar_ros 版本固定在
`scripts/setup/ros2/rplidar-ros.env`，外部源码和所有 colcon build/install/log 输出仍在 `_output/ros2/`。

```bash
# 目录迁移后首次构建：刷新旧源码路径的 CMake 缓存
bash scripts/ros2/build.sh --cmake-clean-cache
# 后续增量构建
bash scripts/ros2/build.sh
bash scripts/ros2/test.sh
```
