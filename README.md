# Smart Cockpit System

运行在 Jetson/Linux 上的模块化智能座舱系统。

当前重点是单机车端链路：车辆状态、音频、语音、摄像头、Qt/QML UI、诊断工具和部署。云端
平台、Web 前端和量产级安全能力暂不属于当前阶段。

## 当前能力

- SocketCAN/mock 车辆数据和 VehicleState gRPC streaming。
- ROS 风格 `topic list/info/echo/hz` 调试。
- ALSA 录音/播放、AudioFrame、SPSC ring、VAD 和语音分段。
- mock ASR/TTS 和可选 whisper.cpp ASR。
- 语音意图、动作分发、车辆状态查询和 HMI handoff。
- V4L2/GStreamer USB 摄像头预览。
- 相机帧 POSIX shared memory 双缓冲。
- 基于最新共享帧的 JPEG 拍照，支持 camera-ctl 和 Qt UI。
- VehicleState 研发录包、会话清单和异常中断恢复。
- Qt 6/QML 车辆和相机界面。
- `cockpit-ctl` 聚合状态和各类 probe/ctl 工具。
- Navigator 统一入口、动态业务模块、运行模式切换和故障重启限制。
- systemd、Release 打包，以及 `safe-ota` 校验、安装、健康检查和失败回滚原型。

## 架构

```text
systemd → cockpit/navigator → cockpit/library
                                  ↓
                           cockpit/modules
                                  ↓
                           cockpit/drivers
                                  ↓
                             cockpit/core
```

- `cockpit/core`：配置、日志、Runtime、事件和 IPC。
- `cockpit/drivers`：ALSA、SocketCAN、V4L2。
- `cockpit/modules`：audio、camera、vehicle、voice 等领域能力。
- `cockpit/navigator`：统一入口、模式、模块进程和状态管理。
- `cockpit/library`：进程级动态业务模块和资源所有权。
- `cockpit/processes`：独立烟测和迁移兼容的薄入口。
- `cockpit/apps`：Qt/QML UI。
- `tools`：模拟器和诊断工具。

```text
cockpit-system/
├── cockpit/   产品源码
├── tools/     诊断与模拟器
├── tests/     测试
├── configs/   运行配置
├── cmake/     构建模块
├── scripts/   构建与部署脚本
└── docs/      文档
```

详细说明见：

- `docs/architecture.md`
- `docs/项目进度总览.md`
- `docs/实现状态.md`
- `docs/运行时通信策略.md`
- `docs/模块化策略.md`

## 环境准备

WSL2/Ubuntu 22.04：

```bash
bash scripts/install_ubuntu_deps.sh
```

## 构建和测试

```bash
bash scripts/build.sh
bash scripts/run_smoke.sh
```

构建目录：

```text
build/x86_64-debug/
build/x86_64-release/
build/arm64-debug/
build/arm64-release/
```

## 常用工具

```bash
build/x86_64-debug/bin/topic list --config configs/config.yaml
build/x86_64-debug/bin/audio-probe --list --config configs/config.yaml
build/x86_64-debug/bin/camera-probe --list --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --start --trigger manual --config configs/config.yaml
build/x86_64-debug/bin/cockpit-ctl status --config configs/config.yaml
build/x86_64-debug/bin/cockpit-ctl health --config configs/config.yaml
build/x86_64-debug/bin/cockpit-ctl runtime status --socket /tmp/cockpit-navigator.sock
```

启动统一运行时：

```bash
build/x86_64-debug/bin/cockpit-navigator \
  --config configs/config.yaml \
  --module-dir build/x86_64-debug/lib/cockpit/modules
```

运行 Qt UI：

```bash
bash scripts/run_cockpit_ui.sh
bash scripts/run_camera_ui.sh
```

## USB 摄像头权限

```bash
sudo usermod -aG video "$USER"
newgrp video
```

随后可执行：

```bash
build/x86_64-debug/bin/camera-preview-probe \
  --device /dev/video0 --frames 30 --config configs/config.yaml
```

## Whisper

模型不提交到仓库。启用 whisper.cpp：

```bash
bash scripts/build.sh -- \
  -DBUILD_WHISPER_CPP_ASR=ON \
  -DWHISPER_CPP_DIR=/home/ffz/code/third_party/whisper.cpp \
  -DWHISPER_CPP_MODEL_PATH=/path/to/ggml-small.bin
```

## 提交规范

```text
[feature]: add ...
[fix]: handle ...
[refactor]: organize ...
[docs]: update ...
```

每批代码变更同步记录到 `docs/变更记录.md`。
