# Camera V2 PR-03：原生采集 Runtime

基于远端 `main` / `abfcc4f636ffe4fa0b4b7d3c6d9bf0b3c666b703` 提取实现。
本轮将原生 probe 的 Argus Session、NvBufSurface 和 Buffer 生命周期移入模块库，
probe 只保留参数解析、采集循环、性能测量和报告输出。生产 GStreamer Camera 服务继续使用原链路。

## 使用接口

模块入口为 `cockpit/modules/camera/capture/native_argus_capture.h`，链接 `native_camera_capture`。
它是同步采集基础组件，不继承要求 CPU 图像的 `CameraPreviewSource`；后续 PR-04 再通过适配器接入旧预览接口。
`NativeFrame` 和 `FrameLease` 位于 `cockpit/modules/camera/frames/native_frame.h`。

```cpp
cockpit::camera::NativeArgusCapture capture;
cockpit::camera::NativeCaptureConfig config;  // 1920x1080@30, 6 buffers
std::string error;
if (!capture.Start(config, &error)) {
  // Report unsupported SDK, unavailable device, unsupported mode or startup error.
  return;
}
{
  cockpit::camera::FrameLease lease;
  const auto result = capture.Acquire(&lease, 5000, &error);
  if (result == cockpit::camera::NativeAcquireStatus::kFrame) {
    const auto& frame = lease.frame();
    // Use frame.dmabuf_fd and metadata only inside this lease's lifetime.
    // Complete all synchronous CPU/device access before returning the lease.
  }
}  // Returns the frame once; Stop/next Acquire flushes the return to Argus.
const auto stopped = capture.Stop(&error);
// Check stopped == NativeStopStatus::kStopped and capture.stats().ResourcesBalanced().
```

## 生命周期合同

`FrameLease` 不可复制，可以移动到同进程内其他线程；`Reset()` 或析构归还一次。
`NativeFrame` 只保存借用的 DMA-BUF FD、缓冲索引、会话代号、尺寸/格式和帧元数据，
不拥有 FD，也不将 DMA-BUF 填入 `CameraFrame::data`。禁止调用者关闭借用 FD。
NV12 为 BLOCK_LINEAR 布局，不能把 FD 或硬件 Surface 当作线性 CPU 图像地址使用。

运行期所有控制方法串行执行。租约在其他线程归还时只进入一个容量受帧池大小约束的返回队列，
Argus 的 `releaseBuffer` 由下一次 `Acquire` 或 `Stop` 执行。返回队列不是 Frame Broker，
不包含消费者分发、录像队列或丢帧策略。

- `Acquire` 只接受空租约，避免意外覆盖消费者仍在使用的帧。
- 全部缓冲都被租约持有时返回 `kNoBuffer`，不会等待消费者后再覆盖其图像。
- `kTimeout` 不关闭 Session；调用方可以继续 Acquire，或正常 Stop 后再次 Start。
- `Stop` 首先执行 `stopRepeat` 和 EOS。有租约未归还时返回 `kLeasesOutstanding`，保留 Session、EGLImage 和原生存储。
  此时不再允许新 Acquire 或 Start；已持有的帧仍有效。归还全部租约后再次 Stop，才执行最终排空和释放。
- Runtime 先析构时仍先请求停止，租约继续保有 Session。最后一个租约析构后完成排空和释放；
  这种延迟关闭可能在最后归还租约的线程执行并等待驱动，调用者应优先显式 Stop，以便检查关闭错误和计数。

正常关闭顺序为：停止重复请求 → EOS → 刷新已归还租约 → drain → waitForIdle →
销毁 Request/Argus Buffer → CPU/EGL 解除映射 → NvBufSurface 释放 → Stream/Session/Provider/EGL 释放。
drain 和 idle 使用有界等待，异常路径尝试 cancelRequests。若驱动仍不能进入 idle，
报告失败并保留可能被 DMA 使用的原生存储至进程退出，拒绝在同一 Runtime 再次启动。
不增加 Supervisor，也不在库内重启 daemon；未来仍由已有 Navigator 故障监管负责进程级恢复。

`NativeCaptureStats` 累计 starts、acquired、leases_returned、实际 buffers_released、drained、
timeouts/no_buffer、持有/待归还/峰值租约、Surface 分配/释放、FD 导入、CPU/EGL 映射及关闭错误。
`ResourcesBalanced()` 还检查 acquired → returned → released 三项计数相等，不能只凭 Surface 分配/释放相等判定成功。

每帧保留 Argus Capture ID、Sensor Timestamp、Frame Duration，以及主机接收的 realtime/steady 时间。
沿用现有源时钟与接收时钟分离原则；Argus sensor clock 暂标 `kUnknown`，不假设等于 Linux monotonic。
Capture ID 也不作为传感器嵌入的硬件帧计数使用。

当前仍为 `SYNC_TYPE_NONE`：生产者写完后 acquire，消费者访问结束后归还。
首帧 `SampleLuma` 在有效租约内验证 CPU Map/SyncForCpu/UnMap。
这不代表已实现 GPU 异步 fence、跨进程租约或完整零拷贝链路。
缓冲访问边界参照 NVIDIA 的
[IBufferOutputStream 文档](https://docs.nvidia.com/jetson/archives/r36.4.3/ApiReference/BufferStream_8h_source.html)。

## 构建与验收

```bash
cmake --build _output/build/arm64-debug --target \
  camera-diag camera-stress libargus-capture-probe \
  native_argus_capture_hardware_test frame_lease_test -j4

# 原有 probe 命令继续有效，内部已使用 NativeArgusCapture + FrameLease。
_output/build/arm64-debug/bin/libargus-capture-probe \
  --device 0 --width 1920 --height 1080 --fps 30 \
  --frames 3000 --cycles 1 --output _output/runtime/camera-native.json

# 只验收正常生命周期、10 次启停及超时恢复，不执行 SIGKILL。
python3 scripts/tests/jetson-camera-v2-gate.py \
  --normal-only --output-dir _output/runtime/camera-pr03-normal-new

# 完整验收额外执行 SIGKILL，异常关闭单独出报告。
python3 scripts/tests/jetson-camera-v2-gate.py \
  --output-dir _output/runtime/camera-pr03-full-new
```

运行前停用旧 Camera 服务，确保 IMX219 空闲。普通单元测试不自动启动硬件；
`native_argus_capture_hardware_test` 是手工/硬件 gate 显式运行的可执行文件，不加入普通 CTest。
硬件测试覆盖持帧 Stop、池耗尽、非空租约拒绝、跨线程归还、同 Session 超时恢复、
同 Runtime 重启，以及 Runtime 先析构而租约继续存活的情况。

SDK 检测从工具 CMake 移入 `cmake/jetson_camera.cmake`。无 SDK 或设置
`-DCOCKPIT_ENABLE_NATIVE_ARGUS=OFF` 时构建 stub：`IsSupported()` 返回 false，Start 明确拒绝，
不生成模拟帧。原生 SDK 不会成为生产 `camera` / CameraService 的新增链接依赖。

报告拆为：

- `camera_native_runtime_gate.json`：正常生命周期与超时恢复验收。
- `camera_abnormal_exit_gate.json`：SIGKILL、即时重连、后续重连及 daemon 重启验收。
- `camera_v2_gate.json`：总结果。异常退出仍失败时总结果仍为失败，不会被正常路径的成功覆盖。
- `native-continuous.json` / `native-restart.json`：每周期性能与计数；`*.frames.tsv` 保留每帧元数据。
- `native-runtime-lifecycle.json`：租约生命周期实测；`*.journal.txt` 保留日志，包括注入超时产生的预期错误。

本轮硬件证据保存到 `docs/verification/cockpit/camera-pr03/`。PR-00/01/02 的历史基线和失败证据保留在原目录。
本轮不实现 Frame Broker、BGRx 兼容转换、H.264、ISP UI 或事件录像。

## 2026-10-24 实测结果

正常路径 [验收报告](verification/cockpit/camera-pr03/camera_native_runtime_gate.json) 为 `passed=true`。

| 检查 | 实测 |
| --- | --- |
| Native 1080P30 连续采集 | 3000 帧，30.006167 FPS；Capture ID 缺口、重复、回退及时间戳异常均为 0 |
| 连续采集租约闭环 | acquired / returned / released = 3000 / 3000 / 3000 |
| 连续采集 Surface | 分配/释放 6/6；EGL Map/UnMap 6/6；首帧 CPU Map/UnMap 1/1 |
| 同一个 Runtime 10 次正常启停 | 共 600 帧，Surface 分配/释放 60/60，acquired / returned / released = 600 / 600 / 600 |
| 生命周期专项 | 持帧 Stop、池耗尽、跨线程归还、同 Session 超时恢复、同 Runtime 重启、租约晚于 Runtime 析构全部通过 |
| 进程内 acquired 超时、重新采集 | 通过；独立 probe 的 1 ms 超时注入及随后重新采集也通过 |
| 生产兼容 | CameraService/帧/Software ISP/时间戳/诊断相关 8 项普通 CTest 全部通过，生产 Camera 模块重新构建通过 |
| SDK 关闭构建 | `frame_lease_test` 通过；probe 无模拟帧，明确报告 SDK 不可用 |

连续采集进程 CPU 为 1.7158%（100%=一个 CPU 核），峰值 RSS 为 44836 KiB。
该测量只覆盖 Native NV12 采集、元数据记录和首帧少量 CPU 读验证，不包含旧 BGRx 转换、Qt 或编码，
不能直接当成端到端预览相对 V1 的性能提升。

**SIGKILL 异常退出验收仍失败。** [独立报告](verification/cockpit/camera-pr03/camera_abnormal_exit_gate.json)
保留即时重连失败和紧随其后的第二次重连 connection refused。daemon 清理 outstanding buffers 后 SIGSEGV，
由 systemd 自动重启，`NRestarts` 从 2 增至 3。原始 [Argus 日志](verification/cockpit/camera-pr03/argus.journal.txt) 保留完整过程。
daemon 启动稳定后补充的 [300 帧检查](verification/cockpit/camera-pr03/after-daemon-active.json) 通过，30.006330 FPS。
这项补充没有覆盖前两次失败，原始异常报告与总 gate 仍保留失败状态；没有手动重启 daemon。

PR-03 的 7 项交付清单均已有实现或取证：Session/缓冲初始化提取、RAII 原生帧、acquire/use/release、
正常关闭流程、元数据与资源统计、连续采集/正常启停/超时恢复、SIGKILL 独立失败报告。
下一轮可以在隔离模式开展 PR-04；异常恢复门槛解决前继续保留现有生产 GStreamer 链路。
