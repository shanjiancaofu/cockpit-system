# 实现状态

更新时间：2026-07-06。

项目范围：`cockpit-system` 是 Jetson 车机端系统。当前优先稳定单机车辆、音频、语音、相机和
Qt UI 链路，云端前后端暂缓。

## 已完成

### 工程基础

- C++17、CMake、Ninja 构建体系和 WSL/Linux 脚本。
- `core/config` 类型化 YAML 配置、启动校验和不可变配置模型。
- 日志、参数解析、信号退出、时间工具。
- Runtime module 生命周期：顺序启动、逆序停止、失败回滚和状态查询。
- POSIX shared memory 通用映射封装。
- protobuf/gRPC 自动生成及 `contracts` target。
- pre-commit、clang-format、clang-tidy、CTest 和 smoke test。
- Release 安装、版本目录、systemd、校验和、健康检查与回滚脚本。

### 车辆与 CAN

- 平台无关 `CanFrame`、原型 `VehicleCanCodec`。
- SocketCAN RAII 封装，支持 `vcan0`。
- `can-simulator` 支持 stdout 和 SocketCAN 后端。
- `vehicle-data-service` 支持 mock/SocketCAN source。
- VehicleState gRPC streaming 到 `cockpit-gateway-service`。
- gateway 提供最新状态快照和事件流。
- `topic list/info/echo/hz` 可调试 `/vehicle/state`。

### 音频与语音

- ALSA PCM RAII、设备枚举、录音、播放和错误恢复。
- PCM16、WAV、20 ms `AudioFrame`、固定容量 SPSC RingBuffer。
- 非阻塞 ALSA poll 采集线程、XRUN 恢复、超时和运行指标。
- Energy VAD、dBFS、语音/静音状态、SpeechSegmenter、pre-roll 和端点切分。
- mock ASR 和可选 whisper.cpp adapter。
- whisper.cpp 多语言模型已在 WSL CPU 完成真实推理测试。
- transcript gRPC streaming、重放历史和断线重连。
- 确定性意图、动作分发、车辆状态查询和 HMI handoff。
- mock TTS、异步播放队列、取消、失败和重连指标。

### 相机

- V4L2 设备发现、capability 和格式查询。
- GStreamer `v4l2src → appsink` 预览 pipeline。
- USB 摄像头 `/dev/video0` 已完成 30 帧真实采集验证。
- camera-service gRPC list/start/stop/status 控制面。
- 相机帧 POSIX shared memory 双缓冲。
- 源端跳帧、最后帧时间、卡帧和 writer 退出检测。
- Qt UI 共享内存 reader、自动重连和 Camera 页面。

### UI 与诊断

- Qt 6/QML 仪表与相机页面。
- 车辆数据 live/stale/disconnected 状态。
- 相机 waiting/live/stalled/last-frame/disconnected 状态。
- `cockpit-ctl status --watch` 聚合 gateway、audio、voice、camera 状态。
- `audio-probe`、`camera-probe`、`camera-preview-probe`、`camera-ctl`、`voice-ctl`。

## 尚未完成

### 车辆产品化

- 基于正式 DBC 或信号定义的量产 CAN 映射。
- 真实车辆读写权限、安全策略和故障降级。
- Jetson 上的 CAN、GPIO、I2C 等硬件适配验证。

### 音频与 AI

- 真实 TTS provider。
- Jetson 麦克风、扬声器、AEC、增益和阈值标定。
- WebRTC VAD、唤醒词、打断和连续对话。
- whisper.cpp Jetson CUDA 或 TensorRT 性能验证。
- LLM provider、上下文、工具调用和隐私策略。

### 相机与视觉

- CSI 摄像头和 Jetson GStreamer pipeline 验证。
- 拍照、录像和研发录包服务。
- 目标检测、驾驶员监控等视觉 AI。

### 存储与外部系统

- SQLite 状态、事件和录包索引。
- MQTT 客户端和真实云端上传。
- WebSocket 浏览器数据流。
- TLS、鉴权、OTA、崩溃收集和长期稳定性测试。

## 最近验证

环境：WSL2 Ubuntu 22.04，GCC 11.4、CMake 3.22、Ninja 1.10、Qt 6.2。

```bash
bash scripts/build.sh
bash scripts/run_smoke.sh
```

- 默认 Debug 构建通过。
- CTest 22/22 通过。
- Qt、GStreamer、whisper.cpp 完整构建通过。
- VehicleState、topic、audio、voice、camera 和 cloud placeholder smoke 链路通过。
- `vcan0` SocketCAN 收发已验证。
