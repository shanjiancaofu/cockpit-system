# PR-10 设备验收记录与剩余浏览器验收

日期：2026-11-09（香港时间）。基线产品提交：`de506d3`。
第 1 节记录基线的设备验收；第 5 节记录本轮尚未提交的主动 IDR/验收补丁实测，
不能将后者归于原始 `de506d3`。没有扩展产品模块或切换 WebRTC 技术栈。

## 1. 本次实际执行结果

设备：Jetson ARM64，Jetson Linux R36.5.2，CSI Camera `nvargus://0`。
使用 `COCKPIT_ENABLE_WEBRTC=ON` 的 `_output/build/pr10`，libdatachannel 0.23.3。
独立运行配置保存在 `_output/pr10-device/device.yaml`；默认 development 配置未修改。

**重要：**验收配置显式使用 `services.camera.capture_pipeline: native_argus`。
默认 development 中的 `argus_isp` 不能替代 Native Camera 验收。
Transfer HTTP 在本次设备 LAN 地址 `10.136.13.65:8080` 监听；此地址是现场记录，
不是写入产品默认配置的固定地址。测试结束后已停止本次启动的 Camera、Transfer、Recording。

| 项目 | 实际结果 | 证据/限制 |
| --- | --- | --- |
| Native Camera 720P -> WebRTC -> H.264 解码 | 通过 | 10 秒观察，249 帧，1280×720，解码错误 0 |
| Native Camera 1080P -> WebRTC -> H.264 解码 | 通过 | 10 秒观察，263 帧，1920×1080，解码错误 0 |
| 720P：WebRTC + RTSP 接收 + Qt 进程 + 手动事件 MP4 | 部分通过 | 30 秒内 WebRTC 解码 856 帧，无错误；RTSP 完成 450 个 buffer 接收；Qt 进程存活；不等于 Qt/VLC/浏览器可见画面验收 |
| 事件 MP4 文件 | 解码通过 | 7,362,948 bytes，1280×720，30/1 FPS，时长 29.266666666 秒；`qtdemux ! h264parse ! nvv4l2decoder ! fakesink` 成功到 EOS，不等于播放器人工观看 |
| 真实 Camera 停止后旧 Peer 清理 | 通过 | connected Peer 在停流后从 status 消失，video_available=false；正在解码的 probe 按预期失败，不误报通过 |
| 真实 Camera 重启后新会话 | 通过 | generation 2 -> 3；新会话解码 125 帧，1920×1080，错误 0 |
| Chrome / Edge 720P、1080P 实际播放 | **未执行** | 当前 Jetson 没有 Chrome/Edge；不能用 C++ 接收端代替 |
| Chrome/Edge 刷新、断开重连 | **未执行** | 已准备下面的真实浏览器脚本，但本次未在目标浏览器运行 |
| Qt 可见预览 + VLC + 浏览器 + 事件录像 | **未完成** | Qt 进程并行、RTSP 协议消费、MP4 解码有证据；缺可见画面及真实浏览器/VLC联合观察 |

本次 Camera 状态包含 `native_metrics_valid=true`、递增的 Native capture/VIC/encoder
计数。视频来自真实 Native Camera，不是 `webrtc_session_test` 的生成测试图。
观测帧数只是连续解码证据，不是性能基准或性能验收目标。

本地原始证据（不加入 git，包含实际摄像头产物）：

```
_output/pr10-device/native-720p.json
_output/pr10-device/native-1080p.json
_output/pr10-device/native-720p-parallel.json
_output/pr10-device/native-restart.json
_output/pr10-device/native-after-restart.json
_output/pr10-device/camera-status.json
_output/pr10-device/camera-parallel-status.json
_output/pr10-device/camera-final-status.json
_output/pr10-device/recording-trigger.log
_output/pr10-device/mp4-discover.log
_output/pr10-device/mp4-decode.log
_output/pr10-device/data/recordings/sessions/*/artifacts/event_video_1.mp4
_output/pr10-device/regression.log
```

相关自动回归 **11/11 通过**。新增 `webrtc_browser_evidence_test` 仅验证验收脚本的
判定与失败记录逻辑，不把模拟 WebDriver 结果算作真实浏览器验收。

## 2. 可重复的设备接收/解码 probe

构建（先按 PR-10 主文档配置好固定 libdatachannel 依赖）：

```bash
cmake --build _output/build/pr10 --target \
  cockpit-navigator cockpit_module_transfer cockpit_module_camera_driver \
  cockpit_module_recording camera-ctl recording-ctl cockpit-ui webrtc_live_probe -j4
```

`webrtc_live_probe` 只接入**已运行**的 Transfer，不启动 Camera、不编码、不生成测试图。
它使用当前 SPS 的 profile 创建 recvonly H.264 offer，完成非 Trickle ICE，并通过
RTP depacketizer + libavcodec 实际解码。无首帧、连续三秒无解码进度、尺寸不符、解码错误、
信令失败或 Peer 关闭均返回非零，且尽力 DELETE 自己的会话。它明确输出
`browser_acceptance:false`。

Camera 与 Transfer 启动后，在没有浏览器占用唯一 Peer 名额时运行：

```bash
mkdir -p _output/webrtc-acceptance
_output/build/pr10/bin/webrtc_live_probe \
  --base-url http://JETSON_IP:8080 \
  --width 1280 --height 720 --seconds 10 \
  --output _output/webrtc-acceptance/native-720p.json
```

切换 1080P 时先停止 Camera、以 1920×1080 H.264 输出重新启动，再执行：

```bash
_output/build/pr10/bin/webrtc_live_probe \
  --base-url http://JETSON_IP:8080 \
  --width 1920 --height 1080 --seconds 10 \
  --output _output/webrtc-acceptance/native-1080p.json
```

该工具是可选测试 executable，不是新服务，也没有加入自动 CTest 硬件测试。
**不能与浏览器同时占用默认的一个 Peer 名额。**

## 3. 在 Chrome/Edge 电脑上完成真正实播

不要在 Jetson 上为此引入另一套媒体服务。可以在同一可信局域网的 Windows/Linux/macOS
电脑运行 Chrome 或 Edge，与 Jetson 通信。以下工具只使用 Python 3 标准库和 W3C
WebDriver，不依赖 Selenium、不自动下载浏览器或 driver。

准备与已安装浏览器版本匹配的 ChromeDriver / EdgeDriver，并在浏览器电脑上启动：

```text
chromedriver --port=9515
```

或者 Edge：

```text
msedgedriver --port=9515
```

Driver 保持默认仅本机可访问，**不要**把 WebDriver 调试端口开放到局域网/公网。
把仓库 checkout 到浏览器电脑（或复制验收 Python 脚本），然后在另一个终端运行。
将 `JETSON_IP` 换成当前 Jetson 局域网地址；此时 Camera 必须正在输出对应分辨率。

Chrome 720P：

```bash
python3 scripts/tests/webrtc-browser-acceptance.py \
  --base-url http://JETSON_IP:8080 --browser chrome \
  --width 1280 --height 720 --seconds 10 \
  --output _output/webrtc-acceptance/chrome-720p.json
```

Edge 720P（先退出 Chrome 的验收/连接，切换为 EdgeDriver）：

```bash
python3 scripts/tests/webrtc-browser-acceptance.py \
  --base-url http://JETSON_IP:8080 --browser edge \
  --width 1280 --height 720 --seconds 10 \
  --output _output/webrtc-acceptance/edge-720p.json
```

Windows 可把 `python3` 改为 `py -3`，命令写为一行或使用 PowerShell 的续行语法。
默认启动可见浏览器；只有明确需要无头检查时才加 `--headless`。

脚本必须检查到 driver 返回正确的 Chrome/Edge 浏览器类型，并执行三轮：

1. 初次连接：实际视频尺寸符合预期，`currentTime`、显示帧数、WebRTC `framesDecoded`
   持续增长；只有 ICE connected 但没有画面会失败。
2. 点击断开，确认服务端旧 Session 退出；重连创建新的 Session，重复连续播放检查。
3. 在**仍连接**时刷新页面，等待旧 Peer 回收；重新连接并重复播放检查。

每轮保存截图和原始观测数据，失败保存 JSON 错误及尽力收集的页面诊断。
退出前断开自己创建的连接、关闭 WebDriver session；无法正常关闭 driver 也不报告通过。
尺寸/帧进度证据不能证明画面内容正确，仍须查看截图和实际画面确认没有花屏/黑屏。
该脚本不自动操作 Camera、Qt 或 Recording，不会把这些项误记为已验收。

将 Camera H.264 输出切换为 1920×1080 后，分别用 `--width 1920 --height 1080`
运行 Chrome 和 Edge，保存各自 1080P JSON/截图。不要在默认单 Peer 下同时运行两台浏览器。

## 4. 最后一轮人工联合验收

请用同一套 `COCKPIT_ENABLE_WEBRTC=ON` 构建，显式启用 Native Camera、WebRTC 和 RTSP；
Camera 参数同时包含：

```
--h264-udp-host 127.0.0.1 --h264-udp-port 8556 --recording-udp-port 8557
```

1. 启动 Qt，打开摄像头页，确认可见预览持续更新。
2. VLC 打开 `rtsp://JETSON_IP:8554/camera`，确认可见连续画面。
3. 浏览器打开 `http://JETSON_IP:8080/`，确认实际尺寸和连续画面；保持三者运行。
4. 若 Recording session 尚未启动，先启动，再触发视频事件：

   ```bash
   _output/build/pr10/bin/recording-ctl --config YOUR_CONFIG --start --trigger pr10_acceptance
   _output/build/pr10/bin/recording-ctl --config YOUR_CONFIG \
     --trigger-video --event-id pr10_manual --event-type manual
   ```

   等待事后录像窗口完成，或至少 25 秒，再停止 Recording session；期间浏览器画面不能中断。
   CLI 返回相对 session 目录的 MP4 路径，使用 VLC 等实际播放器打开录像。
5. 保持浏览器连接时停止 Camera：确认 status 中旧 Session 消失。重新启动 Camera，
   点击浏览器重连，确认新 Session 能播放；不要继续使用旧 Peer。
6. 将浏览器证据、Qt/VLC/MP4 观察结果补入本文。全部基本功能通过后结束 PR-10；
   不增加 PR-11，不做全天长稳和性能平台。

**目前不能宣布 PR-10 全部验收完成；剩余阻塞是 Chrome/Edge 与可见联合画面的现场证据。**


## 5. 主动 IDR 与 Firefox 自动验收补充（2026-11-09）

### 主动 IDR

本轮复用现有 CameraControl gRPC，补齐主动 IDR，不引入新媒体模块。
Native Camera 使用 **GOP=300 / 30 FPS**，而不是只依赖默认约一秒的周期关键帧：

- 真实 WebRTC 接入后：`native_encoder_idr_requests` 0 -> 2，
  `native_encoder_forced_idr_frames` 0 -> 2，6 秒解码 150 帧，错误 0。
- 手动 `camera-ctl --request-idr` 返回 accepted，forced-IDR 计数 2 -> 3。
- live probe 发送 1 次 RTCP PLI，6 秒解码 149 帧，错误 0；接入及 PLI 期间
  forced-IDR 计数 10 -> 13。该计数包含 Track onOpen/恢复等请求，并非把每个计数都归因于 PLI。
- 编码器单元测试验证输出实际 NAL type 5 与 SPS/PPS，检查 burst 合并、250 ms 限频、
  Stop/Start 重置、Flush 后拒绝新请求；客户端测试检查异步、超时与取消。
- WebRTC 和队列测试验证 onOpen、PLI 和订阅队列溢出都会主动请求 IDR。

本地证据在 `_output/pr10-idr-device/` 的 `first-peer.json`、`before-peer.json`、
`after-peer.json`、`manual-request.json`、`after-manual.json`、`pli-peer.json`、
`before-pli.json`、`after-pli.json`。

### Firefox 720P 自动解码、重连和刷新

实际运行 Firefox **153.0.4** + GeckoDriver + 已安装的 OpenH264 **2.6.0**：

| 阶段 | framesDecoded | 实际尺寸 | 结果 |
| --- | --- | --- | --- |
| initial | 6 -> 96 | 1280×720 | 通过 |
| reconnect | 7 -> 97 | 1280×720 | 通过，新 Session |
| refresh | 7 -> 97 | 1280×720 | 通过，旧 Peer 回收，新 Session |

本次自动运行使用 **headless**：检查浏览器真实 inbound `framesDecoded`、播放时间、尺寸和
连接状态，不要求 headless 环境的显示帧计数/合成器可用。JSON 明确标注
`rendering_check=headless_decode_only`，不把截图或自动解码替代可见画面人工验收。
用户此前已单独确认 Firefox 720P 手动播放正常。

发现并修正两个**验收工具**问题，没有通过放宽产品 SDP 协商来制造通过结果：

1. GeckoDriver 新建隔离 profile 默认尚未安装 OpenH264，getCapabilities 没有 H264。
   脚本现在在信令前检查实际 H264 能力；可通过 `--firefox-openh264-dir` 只放入本机
   已安装的 codec library / `.info`，不复制用户 profile、Cookie 或登录信息，不自动下载。
2. GeckoDriver 的脚本 sandbox 看不到页面 `let current`；脚本现在在页面自身 realm
   读取该绑定，再调用真实 Peer 的 getStats，避免已播放却误报无连接。

Firefox 启动命令（在已安装 Firefox/GeckoDriver 的浏览器电脑上）：

```text
geckodriver --host 127.0.0.1 --port 9515
```

```bash
python3 scripts/tests/webrtc-browser-acceptance.py \
  --base-url http://JETSON_IP:8080 --browser firefox \
  --width 1280 --height 720 --seconds 10 \
  --output _output/webrtc-acceptance/firefox-720p.json
```

只有新建隔离 profile 缺少 codec 时才增加可选参数：

```text
--firefox-openh264-dir /path/to/gmp-gmpopenh264/2.6.0
```

目录必须是与该浏览器操作系统/架构匹配的已安装 codec 版本目录。
`--headless` 只用于自动解码验收；可见画面验收不要加此参数。
自动原始证据：`_output/pr10-idr-device/firefox-720p-verified.json` 与各阶段 PNG；
失败诊断也保留在 `firefox-clean-profile.json` 和 `firefox-720p-codec.json`，不改写为通过。

### 新发现的未关闭问题：Firefox 1080P SDP Level

同一 Firefox profile 的 1080P 自动实测**未通过**，失败发生在 SDP 协商，不是 ICE 后黑屏：
浏览器 H264 接收能力公布 `profile-level-id=42e01f`（Level 3.1），Native 1080P 码流的
SPS 为 `42c028`（Level 4.0），当前服务端拒绝发送超过浏览器 offer Level 的流。
保存证据为 `_output/pr10-idr-device/firefox-1080p.json`。

没有静默修改 offer、伪造 Level 或跳过匹配检查。需要单独解决/确认目标浏览器的 1080P
接收能力与协商方式，再重跑测试；不能把 C++ 1080P 解码通过当作 Firefox 1080P 通过。
因此目前可以确认主动 IDR 和 Firefox 720P 自动闭环，**仍不能宣布 PR-10 全部验收结束**。

本轮最终软件回归：**14/14 通过**，见 `_output/pr10-idr-final-regression.log`。
receiver 与 H264Encoder/主动 IDR 路径单独 ASan/UBSan 执行通过，见
`_output/pr10-idr-sanitizers/`；第三方库并未全部插桩，不能当作全依赖 sanitizer 或长稳验收。
WebRTC OFF 的 RTSP/stub 构建和无编码依赖的 encoder stub 编译也通过。
本次启动的设备验收服务和 GeckoDriver 已停止；没有修改默认 development 配置，也未提交代码。
