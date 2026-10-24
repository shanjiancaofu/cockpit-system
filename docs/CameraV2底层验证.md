# Camera V2 底层验证

本轮交付 PR-00、PR-01 和隔离的 PR-02 最小验证程序。生产服务仍使用原有采集链路。
验收依据为本机 Jetson / IMX219 实测，源码、构建和报告均位于 cockpit-system 仓库。

后续 PR-03 已将原生采集提取为可复用 Runtime，接口与新验收见 [Camera V2 原生 Runtime](CameraV2原生Runtime.md)。
本文表格保留 PR-00/01/02 的历史实测结果。

## 构建与运行

```bash
cmake --build _output/build/arm64-debug --target \
  camera-preview-probe camera-diag camera-stress libargus-capture-probe -j 4

# 每次冻结使用新的目录，避免覆盖已有证据。默认 720P/1080P 各预热 60 帧、测量 300 帧。
python3 scripts/run/camera-v1-baseline.py \
  --output-dir _output/runtime/camera-v1-baseline-new

mkdir -p _output/runtime/camera-v2

# 一个命令完成设备、模式、Controls、拓扑、RAW 采集完整性和本次运行的驱动日志检查。
_output/build/arm64-debug/bin/camera-diag \
  --frames 300 --output _output/runtime/camera-v2/diagnostics.json

# 独立 RAW MMAP 连续启停，不保存每帧 RAW 文件。
_output/build/arm64-debug/bin/camera-stress \
  --cycles 10 --frames 60 --output _output/runtime/camera-v2/stress.json

# 不依赖 GStreamer / BGRx / Qt 的原生采集验证。
_output/build/arm64-debug/bin/libargus-capture-probe \
  --device 0 --width 1920 --height 1080 --fps 30 \
  --frames 3000 --cycles 1 --output _output/runtime/camera-v2/native.json

# 硬件验收：包含超时注入和强制终止本脚本启动的采集进程。
# 应在相机空闲时运行；SIGKILL 测试可能暴露 BSP 的 Argus daemon 崩溃。
python3 scripts/tests/jetson-camera-v2-gate.py \
  --output-dir _output/runtime/camera-v2-gate-new
```

原生 Runtime 需要 libargus、NvBufSurface、EGL 开发头文件和库，
可通过 `COCKPIT_JETSON_MULTIMEDIA_ROOT` 设置 Multimedia SDK 目录。PR-03 在缺少 SDK 时构建明确拒绝采集的 stub，
诊断工具仍可构建，probe 会返回不支持的错误。
三项新工具均纳入 CMake 安装清单。采集工具需要独占相机，不能与现有 Camera 服务同时测试。

## PR-00：冻结 V1 基线

采集器输出 `camera_v1_baseline.json`，保存实际命令、Git HEAD、工作区状态、probe/config SHA-256、
BSP、内核、设备/模式、媒体拓扑、电源模式和 NVIDIA 包版本。每档保存 probe 与 tegrastats 原始输出、
进程 CPU/RSS 采样，以及该时间段内核和 nvargus-daemon 日志。缺少的命令和权限会保留错误及退出码。

为使指标可复现，仅修改 preview probe 的测量：支持 `--warmup-frames`，加入单调时钟到帧 FPS，
达到目标帧数后停止计数。采集管线未调整。资源指标包含初始化、预热、测量和停止整个进程阶段。
V1 API 不暴露传感器序号或 V4L2 Buffer flags，因此这两项保存为 `null`，不填成零。

冻结结果位于 [camera_v1_baseline.json](verification/cockpit/camera-v2/camera_v1_baseline.json)：

| BGRx 输出 | 测量帧数 | 单调时钟 FPS | 进程 CPU（100%=一个 CPU 核） | 峰值 RSS |
| --- | ---: | ---: | ---: | ---: |
| 1280×720 | 300 | 30.0055 | 22.9586% | 211924 KiB |
| 1920×1080 | 300 | 30.0063 | 27.0613% | 231860 KiB |

初次无预热的旧工具分别报告 33.1009 / 32.4859 FPS；它使用应用收到帧的墙上时钟时间，
启动交付过程会影响该口径。两次原始输出保存在 `initial-720p.probe.txt` 和 `initial-1080p.probe.txt`。
冻结基线使用预热后的单调时钟测量。这是 V1 交付帧率，不能视为传感器硬件时间戳测量。

## PR-01：Camera Diagnostics

`camera-diag` 复用 `camera-probe` 所依赖的 `V4l2Camera::ListDevices/ListFormats`，
补充帧间隔枚举；复用 `v4l2-mmap-capture` 所依赖的 `V4l2MmapCapture`，补充采集统计。
没有另写一套采集实现，也没有加入驱动补丁。

报告包含：

- `/dev/video*` 能力、驱动版本、卡名称与总线位置。
- `/dev/media*` 实体、pad 和 link，包括 link 是否启用。
- V4L2 离散模式及帧间隔，以及绑定 subdevice 的设备树模式。IMX219 实测枚举 5 个模式。
- 视频节点和 subdevice 的 V4L2 Controls：类型、范围、默认值、标志、菜单、可读标量当前值。
  payload Controls 明确标记 `not_scalar`，不伪造读回；CSI 桥接节点不提供 Controls ioctl 属正常能力差异。
- BSP、内核、IMX219 模块路径/vermagic、BSP 包版本；不存在的模块独立版本字段保持为空。
- 按启停周期统计实际尺寸、帧率、序号缺口/重复/回退、时间戳异常、超时、Buffer Error、I/O 和停止错误。
  每次 Start 重置序号统计，正确处理 32 位序号回绕。协商尺寸和请求不一致、帧率超出 ±5% 均不通过。
- 本次运行的内核/Argus 日志和匹配的错误。日志权限不足、命令失败、超时或输出截断会阻止 `healthy`。
  错误另存为 `<report>.errors.log`。内核错误关联采用相机关键词过滤，保留原始日志供人工复核。

默认诊断会实际启动 RAW 采集。`--inspect-only` 只检查设备信息，返回 `unverified`，
不会因为设备能枚举就报告采集健康。退出码：`0` 健康、`1` 检查失败、`2` 参数或输出错误、
`3` 仅检查而未验证采集。`healthy` 的覆盖范围是 RAW Sensor/驱动及运行期日志；
Hardware ISP 数据路径须另运行原生 probe 验证。

`V4l2RawFrame` 新增完整 `buffer_flags`，保留 `V4L2_BUF_FLAG_ERROR`；出错帧仍正确回队。
采集对象提供结构化 Wait 状态，并返回停止/解除映射/关闭错误。已有调用方式兼容。

Sensor Test Pattern 预留 `SetSensorTestPattern(device, value, error)` 接口，通过标准 Control ID 查询范围与可写性；
诊断命令不会写入 Test Pattern。当前 IMX219 驱动未暴露该 Control，不能把“预留接口”当成硬件功能验收通过。

## PR-02：原生最小验证

验证链路为：`CameraProvider → CameraDevice → CaptureSession → STREAM_TYPE_BUFFER →
NvBufSurface / EGLImage → CaptureMetadata`。

使用 6 个应用分配的 NV12、BLOCK_LINEAR、SURFACE_ARRAY 缓冲；逐个验证 FD 导入、EGLImage 映射、
Argus Buffer 包装与初始释放。获取帧后读取 Capture ID、Sensor Timestamp、Frame Duration，
保存到 `<report>.frames.tsv`；首帧执行 CPU Map、SyncForCpu、少量亮度采样与 UnMap。
这条路径使用 `SYNC_TYPE_NONE`，消费发生于 acquire 后、release 前，不包含异步 GPU 消费。

停止时执行 stopRepeat、EOS、排空、waitForIdle，随后销毁 Argus Buffer、解除 EGLImage 映射并释放 NvBufSurface。
记录分配/释放、EGL 和 CPU Map/UnMap 次数。若驱动无法停止，报告失败并保留可能仍被 DMA 使用的原生内存至
隔离进程退出，避免提前释放；这类结果不会标记资源验证通过。

Capture ID 是 Argus 元数据序号，并非传感器嵌入的硬件帧计数。当前验证不承诺完整零拷贝、跨进程 FD 分发、
异步 fence、消费者背压或生产 Frame Broker。后续 PR-03/04 应以这些边界为基础继续实现。

API 依据为本机 R36.5.2 SDK 头文件，并核对了 NVIDIA 的
[BufferOutputStream 接口说明](https://docs.nvidia.com/jetson/l4t-multimedia/group__ArgusBufferOutputStream.html)。

## 实测结论与下一步门槛

最终硬件验证已通过 1080P 原生连续 3000 帧（30.005967 FPS）、10 次原生启停和 10 次 RAW 启停；
正常采集无 Capture ID 缺口和时间戳异常，原生缓冲分配/释放及映射计数平衡。
1 ms acquire 超时注入返回失败报告，随后原生重新采集通过。

**异常退出恢复尚未通过。** 对正在产帧的 probe 发送 SIGKILL 后，daemon 在清理 6 个 outstanding buffers 时
段错误退出。第一次重新创建 CameraProvider 失败，systemd 自动重启了 daemon。
没有手动重启 daemon，也没有修改驱动来隐藏该结果。硬件 gate 应保留失败状态；
自动重启后能否重新采集会作为独立 follow-up 记录，不能替代即时恢复验收。

最终 [硬件 gate 报告](verification/cockpit/camera-v2/camera_v2_gate.json) 保留 `passed=false`，
`abrupt_recovery=requires_daemon_restart`。两轮强制退出均复现此问题。最终轮 daemon 的 `NRestarts` 从 1 增至 2，
自动重启后的原生 follow-up 成功；[Argus 日志](verification/cockpit/camera-v2/argus.journal.txt) 保留了
outstanding buffers 清理、SIGSEGV、systemd 自动重启及后续重新采集的记录。

| 验收项 | 结果 |
| --- | --- |
| Camera Diagnostics | RAW 1080P30、模式、Controls、媒体拓扑和运行期日志检查通过 |
| Native libargus | 连续 3000 帧，30.005967 FPS，无 Capture ID 缺口和时间戳异常 |
| Native Buffer | 连续采集分配/释放 6/6；10 次启停分配/释放 60/60；EGL 和 CPU 映射配对 |
| Frame Metadata | 每帧 Capture ID、Sensor Timestamp、Frame Duration、DMA-BUF FD 已保存 |
| 正常启停和 acquire 超时恢复 | 通过 |
| 强制退出后直接恢复 | 失败，需要 daemon 自动重启 |
| 物理断流恢复、24h 稳定性 | 未测试 |

相关自动化测试共 7 项通过：`camera_diagnostics_test`、`camera_diagnostics_cli_test`、
`v4l2_mmap_capture_test`、`software_isp_preview_source_test`、`gstreamer_timestamp_mapper_test`、
`camera_frame_test`、`camera_service_test`。生产 Camera 模块和已有 Camera CLI 也完成重新构建。

最终硬件报告与原始日志集中在 `docs/verification/cockpit/camera-v2/`；
物理断流/拔插恢复尚未测试，24h 稳定性也未验收。PR-00/01 可独立评审，PR-02 保持隔离验证，
在异常退出恢复问题解决前不替换生产主链路。H.264、WebRTC、RTSP、云端、事件录像、模型、ALSA 和 OTA 不在本轮范围；
Software ISP 保留现有独立 RAW/NEON 测试用途。
