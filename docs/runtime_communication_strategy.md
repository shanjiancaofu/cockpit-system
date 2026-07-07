# Runtime 通信策略

## 目标

车端系统不采用“所有模块都拆成 gRPC 微服务”的方式。通信机制按线程、进程、数据大小和实时性
选择，减少不必要的序列化、复制和故障点。

## 分层选择

| 场景 | 当前机制 | 适用数据 |
|---|---|---|
| 同线程 | 函数调用 | 同步控制、纯计算 |
| 同进程低频 | callback / EventQueue | 状态变化、控制事件 |
| 同进程连续流 | SPSC RingBuffer | PCM、固定帧数据 |
| 跨进程大数据 | POSIX Shared Memory | 相机帧，未来视频/模型输入 |
| 跨进程控制 | gRPC unary | start/stop/status/config |
| 跨进程小消息流 | gRPC streaming | VehicleState、transcript、事件 |
| 跨机器 | MQTT/WebSocket/HTTP | 云端和浏览器，后续实现 |

## 控制面与数据面

控制面负责：

- 生命周期管理。
- 配置、状态和指标。
- 文本命令和调试接口。
- 低频结构化消息。

数据面负责：

- 音频 PCM。
- 相机帧和未来视频流。
- 未来雷达、点云或模型张量。

大数据不直接塞进 gRPC。共享内存传数据本体，控制接口传名称、generation、时间戳和状态。

## 当前链路

### 音频

```text
ALSA capture thread
    → SPSC RingBuffer<AudioFrame>
    → VAD/segment/ASR consumer
```

单生产者、单消费者、固定容量。队列满时丢帧并记录指标，采集线程不得等待 ASR。

### 相机

```text
GStreamer callback
    → SharedFrameWriter
    → POSIX shared memory 双缓冲
    → Qt SharedFrameReader
```

writer 写入非活动槽后切换 generation。reader 只读取最新帧，不积压视频队列。

### 车辆状态

VehicleState 体积小、频率低，当前使用 gRPC streaming。未来只有在频率和消费者数量明显增长时，
才考虑本地 message bus。

## Runtime 边界

`cockpit/core/runtime` 只管理模块生命周期，不承载领域业务。`ModuleManager` 提供顺序启动、逆序停止、
失败回滚和状态查询。

运行管理当前不新增独立 manager service。参考 `znavigator` 的模块/进程编排思想，现阶段采用更轻的
组合：systemd 负责进程启动和重启，`ServiceRuntime` 负责单进程生命周期，`ModuleManager` 负责进程内
模块，`cockpit-ctl status/health` 负责人工查看和脚本化健康检查。`health` 通过各服务 gRPC 控制面
探测 gateway、audio、voice、camera 和 recording，全部健康返回 0，任一不可达或 faulted 返回非 0。

当前不引入通用 Actor、DDS、共享内存 ring 或动态插件系统。只有至少两个真实模块出现相同需求后，
才抽象通用 MessageBus、Scheduler、Recorder 或 Monitor。

## 后续演进

1. 保持单进程模块优先。
2. 出现设备独占、故障隔离或独立部署需求时再拆进程。
3. 小消息使用 IPC/gRPC，大数据使用 shared memory。
4. 摄像头 AI 等新消费者通过模块接口接入，不直接依赖 UI 或 service 实现。
