# Camera V2 PR-04：Native Frame Broker 与 VIC 验证

基于 `main@b47288eee3d96d12331e375a4d7d443fd756cd7c`。本轮复用 PR-03 的采集器和六缓冲池，
增加同进程共享生命周期、两个有界消费队列和同步 VIC 转换。生产服务仍使用 GStreamer；
没有新增独立视频服务、跨进程 FD 租约或 Supervisor。

## PR-04A：共享生命周期

`frames/frame_broker.h` 的 `SharedFrameLease` 可以复制，内部共同持有一个真实的、不可复制的 `FrameLease`。
Native 元数据保持不变：session_id、buffer_index、Capture ID、Sensor Timestamp、Frame Duration 和主机接收时间。
没有复制原生像素，也不另建原生采集分配器。

最后一个共享持有者销毁后，原始 FrameLease 才归还 PR-03 的返回队列；下一次 Acquire/Stop 再执行 Argus release。
`Close` 或切换 Session 会撤销旧共享句柄的元数据访问，`operator bool` 返回 false，`frame()` 抛出 logic_error。
这不会提前释放消费者仍持有的硬件存储。已开始的同步 VIC 操作仍保有输入，完成后若发现撤销则拒绝输出结果。
调用者不得保存借用 FD 后绕过租约继续使用；整数 FD 本身不能提供跨 Session 的生命周期保护。

`BeginSession` 要求 Session ID 严格递增，不允许重新激活旧代次。发布旧 Session 帧会返回 `kWrongSession` 并归还输入。
关闭时队列被排空，等待中的消费者被唤醒。持帧消费者仍须归还全部共享句柄，Runtime 的 Stop 才能完成最终释放。
帧释放均在 Broker 锁外执行，避免最后一次释放触发 Runtime 延迟关闭时阻塞队列锁。

## PR-04B：队列与背压

| 消费者 | 策略 | 默认容量 |
| --- | --- | ---: |
| Preview | Latest-only，新帧覆盖排队旧帧 | 1 |
| Transform | 原始帧 FIFO，满时丢最旧排队帧 | 2 |

Broker 不运行消费者回调，不执行 VIC、Qt 或磁盘操作。`TryPop` 不阻塞；`WaitPop` 有超时并响应 Close/Session 切换。
采集端单线程按采集顺序 Publish，消费者可并发取帧。每个正常消费者处理一个在途帧；
队列深度、投递/取出/丢帧数及高水位分别统计。

只限制队列长度仍不足以保护原生池：消费者已取走的共享帧也会占用 Surface。
`FrameBrokerConfig` 默认 `pool_size=6, transform_capacity=2, capture_reserve=4`，
同一 Session 的共享帧及发布预留总计最多 2 个。预算包括所有消费者持有的共享帧，而非共享引用个数。
预算不足时先驱逐排队帧；仍被在途消费者占满时，返回 `kBudgetDropped`，归还这次采集的输入。
不会强行收回消费者正在访问的 FD。原生 Runtime 的 `kNoBuffer` 仍作为独立错误/压力指标保留。

预留值依据本机 BSP 实测设置：初始仅预留一个缓冲时，100 ms 慢消费者使传感器帧间隔出现周期性约 66.7 ms，
60 帧测得约 22.99 FPS；当时没有触发 NoBuffer，Surface 分配/释放也平衡。
这表明“还有一个空缓冲”不等于满足 Argus 内部流水深度。最终预留四个后，慢消费者测试恢复约 30.006 FPS。
该值是当前六缓冲池与 BSP 的已验配置，不声明为所有 JetPack 的通用要求；更改池/预算须重新测量帧间隔。
若消费者无限保留共享句柄，Broker 会继续丢弃新投递来保护采集；不会保证所有消费者始终收到每帧。

## PR-04C：VIC 与兼容边界

`transform/vic_transform.h` 提供同步转换：

```text
Native NV12 BLOCK_LINEAR / SharedFrameLease
  → NvBufSurfTransform（明确选择 VIC，无 GPU fallback）
  → NvBufSurface PITCH（单个缓存输出 Surface）
  → Map + SyncForCpu + 按平面/stride 复制有效行 + UnMap
  → 独立拥有 CPU 字节的 CpuVideoFrame
```

支持 BGRx、NV12 和 I420 输出及缩放。每个转换器独立缓存一个输出 Surface，格式/尺寸变化时释放再分配；
所有 Map/UnMap 与分配/释放均计数。新工作线程可能尚未配置 SDK session，转换前始终显式 SetSessionParams(VIC)，
存在先前设置时在调用后恢复。没有调用异步 transform，也没有异步 GPU Fence 合同。

CPU 副本保留各平面真实 pitch，明确 offset、有效行字节数和高度；allocator padding 不会暴露给下游，副本 padding 置零。
NV12 保留交错 UV，I420 分别记录 U/V 平面，不把 width 当成 stride。参数、映射范围、输出大小或格式不合法会拒绝。
CPU 输出只保存元数据和自有字节，不继续暴露借用 DMA-BUF FD。

`TakeCameraFrame` 只在 BGRx 兼容边界移动 CPU 字节到已有 `CameraFrame`，保留 Capture ID、源时间与接收时间区分。
Argus Sensor Clock 仍为 Unknown，不把它当成 Unix epoch。真实相机 probe 将 BGRx 交给现有 LatestFrameBuffer；
确定色彩的硬件测试还验证现有 SharedFrameWriter/Reader 和 JPEG 编码。Qt 页面与生产 CameraService 的接入属于后续 PR-06，
本轮没有修改其默认采集入口或声称完成 Qt GPU 渲染。

SDK 依据为本机 R36.5.2 头文件，并核对 NVIDIA
[NvBufSurfTransform API](https://docs.nvidia.com/jetson/archives/r35.1/ApiReference/group__ds__bbb.html)。
无 SDK 或关闭 Native Argus 时构建明确拒绝转换的 stub，不模拟 VIC 成功。

## 构建与运行

相机须空闲，旧 Camera Service 不得与验证程序同时占用 IMX219。

```bash
cmake --build _output/build/arm64-debug --target \
  camera-diag camera-stress libargus-capture-probe native_argus_capture_hardware_test \
  native-frame-broker-probe frame_broker_test vic_transform_test vic_transform_hardware_test -j4

# 两个消费者只读元数据；慢 Transform 每帧持有 100 ms。
_output/build/arm64-debug/bin/native-frame-broker-probe \
  --frames 3000 --mode metadata --slow-ms 100 --output _output/runtime/broker.json

# 两个消费者分别 VIC -> BGRx 和 VIC -> CPU NV12。
_output/build/arm64-debug/bin/native-frame-broker-probe \
  --frames 300 --mode vic --slow-ms 100 --output _output/runtime/broker-vic.json

# 测试图：NV12 Pitch -> NV12 Block-linear -> BGRx/NV12/I420，核对色彩、padding、共享内存及 JPEG。
_output/build/arm64-debug/bin/vic_transform_hardware_test --output _output/runtime/vic-layouts.json

# PR-03 + PR-04 正常路径完整验收，不执行 SIGKILL。
python3 scripts/tests/jetson-camera-v2-gate.py --broker --normal-only \
  --output-dir _output/runtime/camera-pr04-new

# 独立异常退出对照；只强制终止本脚本启动的进程，不手动重启 daemon。
python3 scripts/tests/jetson-camera-exit-compare.py \
  --output-dir _output/runtime/camera-exit-compare-new
```

`frame_broker_test`、`vic_transform_test` 是普通 CTest，不访问相机。
`vic_transform_hardware_test` 仅由手工或硬件 gate 显式运行。
`camera_frame_broker_gate.json` 保存正常验收及同次 Native-only 对照；异常退出结论单独写入
`camera_exit_comparison.json`。正常路径通过不会覆盖异常退出失败。

硬件证据保存在 `docs/verification/cockpit/camera-pr04/`，异常对照保存在其 `exit-comparison/` 子目录。
CPU/RSS 比较只针对相同 Native NV12 采集、元数据和首帧少量采样；带 VIC/CPU 副本的指标单独报告，
不能与 PR-03 的原生采集 CPU 直接等价比较。

## 2026-10-24 真机验收

[PR-04 正常路径报告](verification/cockpit/camera-pr04/camera_frame_broker_gate.json) 为 `passed=true`。

| 项目 | 结果 |
| --- | --- |
| 双消费者连续 3000 帧 | 30.006282 FPS；Capture ID 缺口和时间戳异常均为 0 |
| Preview / 100 ms 慢 Transform | 2999 / 999 帧处理；Preview 队列无丢帧，Transform 排队帧丢弃 2001 次 |
| 池预算与关闭 | 峰值共享帧 2；NoBuffer=0；持帧 Stop 和 Close 撤销检查通过 |
| 3000 帧租约闭环 | acquired / returned / released = 3000 / 3000 / 3000；Surface 6/6 |
| 同 Runtime + Broker 10 次启停 | 600 帧全部闭环；Surface 分配/释放 60/60；Session 1 至 10，无混帧 |
| 双 VIC 消费 300 帧 | 30.006198 FPS；BGRx 297 帧、NV12 93 帧，预算丢弃 1 帧；原生与转换输出资源配对 |
| 确定色彩/平面验证 | 红色 BGRx `[0,0,255,255]`；NV12/I420 色度、padding、现有共享内存和 JPEG 均通过 |

最后一个测量帧专门保留给持帧 Stop 检查，因此不计入 Preview worker 的处理帧数。
每周期 queue 计数为同一个 Broker 的累计统计，关闭排空也计入丢弃数；不要对周期累计值再次求和。
CPU/RSS 为同次运行的进程快照（100% = 一个 CPU 核）：

| 负载 | 帧数 | 进程 CPU | 峰值 RSS |
| --- | ---: | ---: | ---: |
| Native-only 对照 | 3000 | 1.664966% | 44836 KiB |
| Native + Broker，元数据双消费者 | 3000 | 1.886695% | 45944 KiB |
| Native + Broker + 双 VIC/CPU 输出 | 300 | 14.591183% | 155508 KiB |

同口径 Broker 增量约 0.222 个 CPU 百分点、1108 KiB RSS；这是单次环境测量，不代表所有负载的固定开销。
增量还包含两个消费者线程和工具的库装载差异，不等于 Broker 容器自身的内存大小。
VIC 行包含不同的转换/复制负载和较短测量时长，单独列示，不用于计算相对 Native-only 的预览加速比。
本轮不验收真实 Qt 页面帧率、编码性能或 24h 稳定性。

10 项相关普通 CTest 全部通过。关闭 SDK 的构建中，Broker 和平面复制测试也通过；
独立 ASan/UBSan 构建开启泄漏检测后，两项测试通过（平面测试使用 VIC stub，不执行硬件 API）。
TSan 未执行：仓库当前 CTest 启动器只支持 Linux x86_64，本机为 aarch64。
检查清单保存在 [validation.json](verification/cockpit/camera-pr04/validation.json)，
初始预留预算实验及原始时间戳保存在 `reserve-experiment/`，不作为最终验收通过依据。

## 独立 SIGKILL 排查结论

[异常退出对照报告](verification/cockpit/camera-pr04/exit-comparison/camera_exit_comparison.json)
记录同一 R36.5.2 / 5.15.199-tegra、同一 IMX219、1080P30 下的两组显式故障注入。
两组都在观察到至少 30 帧且运行超过 8 秒后，只 SIGKILL 本脚本启动的采集进程组。

| 入口 | kill 前观察到的帧数 | 即时恢复 | daemon 自动重启 | 后续恢复 |
| --- | ---: | --- | --- | --- |
| Native STREAM_TYPE_BUFFER | 226 | 失败 | 是 | 60 帧成功 |
| nvarguscamerasrc / GStreamer NVMM | 209 | 成功 | 否 | 60 帧成功 |

Native 的 [服务端日志](verification/cockpit/camera-pr04/exit-comparison/native-buffer.argus.journal.txt)
再次出现 outstanding buffers 清理后的 SIGSEGV；GStreamer 的
[对照日志](verification/cockpit/camera-pr04/exit-comparison/gstreamer.argus.journal.txt) 本次没有 daemon 重启。
这说明当前两条客户端/缓冲清理路径存在可复现的行为差异；不能由此断言已经定位到具体 NVIDIA 函数，
也不能把一次 GStreamer 故障注入成功推广为所有 BSP 的保证。测试通过 GStreamer 入口进行，
没有额外编写或声称验证独立的 Native EGLStream 实现。

本机 core_pattern 为 Apport，未取得可符号化的 daemon 崩溃栈，根因仍未确定。
下一步排查应带同一复现脚本、BSP/相机包版本和完整日志核对 NVIDIA 对应版本的服务端清理实现/修复，
而非在 Broker 中增加第二套 Supervisor。异常报告 `production_switch_ready=false` 保持原样，
没有通过手动重启 daemon 把即时恢复改判为成功。
因此 Native Broker/VIC 继续用于隔离开发，生产默认 GStreamer 链路继续保留。
