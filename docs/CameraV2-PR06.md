# Camera V2 PR-06：Native Camera 业务链路

PR-06 将 Native libargus 采集接入现有 CameraService：Native NV12 → FrameLease → NativeFrameBroker → VIC BGRx → CameraFrame → 共享内存/Qt。`argus_isp` 仍是默认生产后端，`native_argus` 只在配置中显式启用。

## 真机 Gate

Native-enabled ARM64 构建后，在 Jetson 上停止其他相机占用者，使用 Native 后端配置启动 CameraService，并先确认 `nvargus://0`、1920×1080、30 FPS 预览和拍照。

```bash
cmake --build _output/build/arm64-debug -j4
ctest --test-dir _output/build/arm64-debug --output-on-failure

_output/build/arm64-debug/bin/native_argus_preview_hardware_test 3000 10
```

硬件测试参数依次为每轮有效 BGRx 帧数和启停轮数。测试会检查预览回调、FrameLease Acquire/Return/Release 配对、Broker 无遗留、VIC 映射平衡和 Native Surface 资源平衡。stub 构建只验证明确不支持，不模拟硬件帧。

## Tuning 与指标

`camera-ctl tuning-service` 通过 CameraService 当前 Session 执行 Get/Set/Reset/Apply Profile；`accepted` 只表示 SDK 接受请求，`applied` 需要逐帧 Metadata 中出现对应 revision 才为 true。

Native 预览帧数、P95 交付延迟、Capture Lease 计数、Broker 丢弃/保留数和 VIC 转换统计已经通过 `CameraStatus` 与 `camera-ctl --status` 暴露。P95 从 Native 收帧到 CameraFrame 回调完成，不代表屏幕扫描显示延迟。

Native BufferOutputStream 异常退出导致 `nvargus-daemon` 崩溃仍是独立 P0，不由本 Gate 的正常启停结果覆盖。

## 2026-10-27 真机结果

- Native Preview Gate `3000 帧 × 10 轮` 退出码为 0；`nvargus-daemon` 保持 active，未发生重启。
- CameraService 业务链路运行在 1920×1080@30：累计 6209 帧、0 丢帧，Native Capture Acquire/Return/Release 最终为 6212/6212/6212。
- Native Preview 指标：`delivery_p95_ms` 约 15.96ms，VIC 转换 6209 次，Broker budget drop 为 0。
- PhotoService 保存 1920×1080 JPEG，文件约 1.87MB，序号和时间戳有效。
- Tuning RPC 同一 Session 验证通过：revision 1 请求返回 `accepted=true, applied=false`，随后逐帧 Metadata 观察到 revision 2 后 `applied=true`；过期 revision 被拒绝。
