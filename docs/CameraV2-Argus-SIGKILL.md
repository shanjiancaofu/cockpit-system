# Native Argus SIGKILL 修复与验收（2026-11-11）

## 结论及边界

本项目默认 NativeArgusCapture 路径已改为 **Argus-owned EGLStream → 同步复制到有界
NvBufSurface 池 → FrameLease**。在 R36.5.2 / 5.15.199-tegra / IMX219 增强版 BSP 上：

- 正式 Native probe 连续 5 轮“已产帧 → SIGKILL → 立即重新采集 → follow-up”全部通过，
  daemon PID、InvocationID、NRestarts 均未变化。
- 对正在 WebRTC/RTSP/MP4 联合测试的真实 camera_driver 子进程 SIGKILL 后重建子进程，
  浏览器、原 RTSP TCP 连接和事件录像均恢复；daemon 未重启。

这是本项目 Native 取帧实现的用户态修复，不是 NVIDIA 旧 BufferOutputStream 二进制的补丁。
不再使用该旧接口注册客户端 EGLImage；旧路径的已复现失败和 Core 证据保持为历史事实。
未对所有 JetPack 版本、任意 GPU/内核故障、物理掉线或 24h 长稳作保证。

## 为什么调整缓冲所有权

旧路径在客户端分配 NvBufSurface/EGLImage，并以 STREAM_TYPE_BUFFER 注册给 Argus；
客户端被 SIGKILL 后不可能执行析构归还。旧实机日志在 daemon 清理 6 outstanding buffers
后记录 SIGSEGV，即时重建 CameraProvider 失败；只有 daemon 重启后能恢复。

尚未读取 root-only 的 Apport Core，因此没有宣称定位到闭源库的某一行代码。修复依据是
消除已复现的跨进程外部缓冲清理触发条件，而不是添加 signal handler（SIGKILL 无法处理）、
保活进程、特权 restart、systemd 策略修改或无限重试。

新路径：

```
Native libargus / Hardware ISP
    → STREAM_TYPE_EGL（Argus producer 拥有采集缓冲）
    → 原生 EGLStream::FrameConsumer 获取 Frame 与同帧 CaptureMetadata
    → IImageNativeBuffer::copyToNvBuffer（同步复制）
    → 现有有界 NvBufSurface / DMA-BUF 池
    → FrameLease / Frame Broker → VIC → 单实例 libx264 → RTSP / WebRTC / MP4
```

Argus Frame 在 Acquire 返回之前自动归还；外部消费者只能持有独立池的 FrameLease，
不能影响 Argus producer 的缓冲所有权。客户端池不再传入 IBufferOutputStream::createBuffer。
保持既有 Start/Acquire/Stop、held lease、跨线程归还、metadata revision、调参和下游接口。
正常 Stop 使用 stopRepeat、FIFO drain、waitForIdle 后断开 EGLStream；仍为有界等待，
不为异常清理阻塞增加后台 supervisor。未改 Sensor/VI/NVCSI、HMI 或编码器。

实现直接参考本机固定 NVIDIA SDK 接口声明：

- `/usr/src/jetson_multimedia_api/argus/include/Argus/EGLStream.h`
- `.../EGLStream/FrameConsumer.h`：Frame 的析构归还 producer 资源。
- `.../EGLStream/ArgusCaptureMetadata.h`：获取同一 Frame 的 CaptureMetadata。
- `.../EGLStream/NV/ImageNativeBuffer.h`：copyToNvBuffer 复制到调用者已有的 DMA-BUF。

## 成本：不是原来的零复制路径

增加一次 native surface copy 和 Argus 内部 producer 缓冲；仍不重编码、不引入 GStreamer
Camera、不做 CPU 软件 ISP。不能宣称仍是原来的直接注册零复制路径。

| 短时样本 | 帧数 | copyToNvBuffer 总耗时 | 平均每帧复制耗时 | 传感器实测帧率 |
| --- | ---: | ---: | ---: | --- |
| 720P30 | 240 | 1220.872 ms | 5.087 ms | 约 30.006 FPS |
| 1080P30 | 240 | 1861.567 ms | 7.757 ms | 约 30.006–30.007 FPS |

这些是本轮 SDK 调用的墙钟样本，不是纯 CPU 时间，也不是整个端到端延迟或性能保证。
两组 Capture ID gap/duplicate/timestamp error 均为 0，客户端池/映射/FrameLease 计数平衡。
新增 probe 的 surface_copy_frames / surface_copy_total_ms 明示该成本。
既有 buffers_released 计数现在表示客户端池归还，不冒充 SDK releaseBuffer 调用次数。

## 硬件验收

### 5 次正式 Native SIGKILL

`native-five-kills/camera_exit_comparison.json`：comparison_completed=true、
production_switch_ready=true，5/5 immediate recovery 与 follow-up 均通过。每轮在观察到
232 帧后杀进程，真正的 SIGKILL exit=-9；报告记录 stream_type=STREAM_TYPE_EGL。
全过程 PID=17696、NRestarts=1、InvocationID 不变。NRestarts=1 是之前旧路径失败留下的
累计值，本轮没有新增重启。

### FrameLease、调参与普通采集

hardware-regression.json 的 6 个定向检查全部通过：

- Lifecycle：held lease Stop、耗尽池、跨线程归还、超时恢复、重复启停、lease 超出 runtime
  生命周期。新增持有一个池 slot 时继续获取 12 帧的像素不变检查，确认 producer/其他 copy
  不覆盖仍持有的 slot；surface_copy_frames=frames_acquired。
- 并发 Acquire/SetTuning、held Metadata 不变、Session/Revision 重置。
- Profile 十轮、非法配置拒绝；AE/AWB/锁定/Reset 三轮、720 帧 Metadata。
- 720P30 和 1080P30 各 120 帧 × 2 cycles，均资源平衡。

### 真正 camera_driver 进程 SIGKILL

`media-camera-sigkill/acceptance.json`：passed=true、daemon_stable=true。

- 杀的是该脚本自建、正在产帧的 camera_driver，PID=23200，确认 exit=-9。
- 只重建 Camera 子进程，不重启 Transfer/Recording/Argus。
- WebRTC Generation 1→2、旧 Peer 退休、新 Peer 不同；新连接 5 秒解码 2→153 帧。
- 原 RTSP TCP 连接产帧观察计数 32→343；此前暂停慢 TCP 接收端时，健康端 1200 buffer
  与 Chrome initial/reconnect/refresh 也正常。
- 连续两次事件及杀进程恢复后的第三次事件均完成；三个 MP4 均发现/解码到 EOS，Chrome
  原生 MP4 播放器实际推进；最终 Session HEALTHY、3 文件、0 issue。
- 运行快照仍有 source_frames_skipped=3，不把浏览器零丢包扩展为全链路绝对零跳帧；
  运行中归还计数可能相差一个在途帧，单独生命周期测试才用来判断停止后的资源平衡。
- Kernel journal 无新增条目；旧 Apport 文件的时间戳仍为旧失败的 18:05，没有新增 Core。

软件 CTest 18/18 通过（含 Camera/Frame Broker/Tuning、Recording、RTSP/WebRTC）。
VLC、Qt/HMI 本轮不纳入；Firefox 本轮未重测，历史结果不重计；浏览器仍按 720P。

## 复现（明确故障注入，只作用于自建进程）

```bash
cmake --build _output/build/pr10 --target \
  libargus-capture-probe cockpit_module_camera_driver camera-ctl -j2

python3 scripts/tests/jetson-camera-exit-compare.py \
  --build-dir _output/build/pr10 --native-only --cycles 5 \
  --output-dir _output/runtime/YOUR_FRESH_SIGKILL_DIRECTORY

python3 scripts/tests/native-media-recovery.py \
  --build-dir _output/build/pr10 \
  --output-dir _output/runtime/YOUR_FRESH_MEDIA_DIRECTORY \
  --base-url http://JETSON_LAN_IP:8080 \
  --chromedriver /absolute/path/to/chromedriver \
  --chrome-binary /absolute/path/to/google-chrome \
  --kill-camera-child
```

不要与其他 Sensor 消费者同时运行。普通 Camera Session Stop/Start 验收省略
--kill-camera-child；默认不会给 Camera 发送 SIGKILL。无需 sudo、驱动部署或系统重启。

原始证据均不入 git：

```
_output/runtime/argus-kill-fix/egl-kill-validation.json        # 独立原型 3 次 kill
_output/runtime/argus-kill-fix/native-five-kills/               # 正式 probe 5 次 kill
_output/runtime/argus-kill-fix/hardware-regression.json
_output/runtime/argus-kill-fix/{lifecycle,concurrent,profiles,tuning}.json
_output/runtime/argus-kill-fix/{normal-720,normal-1080}.json
_output/runtime/argus-kill-fix/media-camera-sigkill/             # 真正模块 kill / 联合链路
_output/runtime/argus-kill-fix/ctest.log
```
