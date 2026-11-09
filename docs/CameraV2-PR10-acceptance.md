# PR-10 设备验收记录与剩余浏览器验收

> 当前验收范围（`eb46f7b` 后续）：**Qt / HMI = Out of Scope**。不再启动、测试或修改
> Qt/HMI，也不把历史 Qt 未验收记录作为阻塞。当前状态以第 7 节为准；第 1～6 节保留历史证据。
> 本轮收尾仅涉及 H.264 接收能力协商、RTSP/WebRTC/事件 MP4 和原生 Web 控制台。


日期：2026-11-09（香港时间）。基线产品提交：`de506d3`。
第 1 节记录基线的设备验收；第 5 节记录已提交到 `63fbe91` 的主动 IDR/验收补丁实测，
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
| Chrome / Edge 720P、1080P 实际播放 | **未执行** | 该次 Jetson 尚未安装 Chrome/Edge；后续 Chrome 实测见第 6 节，不能用 C++ 接收端代替 |
| Chrome/Edge 刷新、断开重连 | **未执行** | 已准备下面的真实浏览器脚本，但本次未在目标浏览器运行 |
| Qt / HMI | **Out of Scope** | 已移出当前验收范围，不再测试或修改；三路视频验收见第 7 节 |

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
  cockpit_module_recording camera-ctl recording-ctl webrtc_live_probe -j4
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

1. VLC 打开 `rtsp://JETSON_IP:8554/camera`，确认可见连续画面。
2. 浏览器打开 `http://JETSON_IP:8080/`，确认实际尺寸和连续画面；保持两者播放。
3. 若 Recording session 尚未启动，先启动，再触发视频事件：

   ```bash
   _output/build/pr10/bin/recording-ctl --config YOUR_CONFIG --start --trigger pr10_acceptance
   _output/build/pr10/bin/recording-ctl --config YOUR_CONFIG \
     --trigger-video --event-id pr10_manual --event-type manual
   ```

   等待事后录像窗口完成，或至少 25 秒，再停止 Recording session；期间浏览器画面不能中断。
   CLI 返回相对 session 目录的 MP4 路径，使用 VLC 等实际播放器打开录像。
4. 保持浏览器连接时停止 Camera：确认 status 中旧 Session 消失。重新启动 Camera，
   点击浏览器重连，确认新 Session 能播放；不要继续使用旧 Peer。
5. 将浏览器证据、VLC/MP4 观察结果补入本文。全部基本功能通过后结束 PR-10；
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
该次设备验收服务和 GeckoDriver 已停止；默认 development 配置未修改。主动 IDR 与验收工具现已提交到 `63fbe91`。


## 6. `63fbe91` 后续 Chrome 与联合验收（2026-11-09）

本轮以 `63fbe91` 为产品基线，没有修改 Camera、H.264、RTSP、Recording、WebRTC 服务端
或网页 UI；只给现有浏览器验收脚本增加可选 `--browser-binary`，支持非标准路径的真实浏览器，
并补充对应单元测试与本节记录。第 5 节的“尚未提交”表述已更正为提交 `63fbe91`。

### Chrome 720P：真实有界面浏览器通过

设备上已安装原生 ARM64 **Google Chrome 157.0.8103.38**。本轮从官方 Chrome for Testing
分发准备了完全匹配的 **ChromeDriver 157.0.8103.38 / linux-arm64**，只放在 `_output/`，
没有在产品配置/构建/运行时加入自动下载依赖。来源、版本和下载 SHA-256 保存在
`_output/pr10-chrome-acceptance/tools/toolchain.json`。

实际使用 `--browser chrome`，**未启用 headless，也没有关闭浏览器 sandbox**。
复用现有脚本，不创建另一套测试程序。独立 Native Camera / Transfer 配置仍启用
`native_argus`，720P H.264 只编码一次。

| 阶段 | framesDecoded | 实际尺寸 | 丢包 / 丢帧 | 结果 |
| --- | --- | --- | --- | --- |
| initial | 10 -> 310 | 1280×720 | 0 / 0 | 通过 |
| reconnect | 10 -> 310 | 1280×720 | 0 / 0 | 通过，新 Session |
| refresh | 10 -> 308 | 1280×720 | 0 / 0 | 通过，旧 Session 回收，新 Session |

各轮连续采样 10 秒，浏览器播放时间、显示帧计数和 `framesDecoded` 持续增长。
WebDriver 页面截图确认真实摄像头画面，不是仅验证 ICE connected。截图是浏览器自己的
渲染结果；不把它等同于对物理显示器的人工观察，也不代替 Qt/VLC 的画面验收。

证据：`_output/pr10-chrome-acceptance/chrome-720p.json` 与 `chrome-720p-*.png`。

非标准安装路径的启动示例：

```bash
chromedriver --port=9515
python3 scripts/tests/webrtc-browser-acceptance.py \
  --base-url http://JETSON_IP:8080 --browser chrome \
  --browser-binary /path/to/google-chrome \
  --width 1280 --height 720 --seconds 10 \
  --output _output/webrtc-acceptance/chrome-720p.json
```

`--browser-binary` 是 WebDriver 主机上的浏览器路径，标准安装可省略；此参数不会添加
`--no-sandbox` 等降低安全性的启动选项。

### Chrome 1080P：同样在 SDP Level 阶段失败

Camera 实际切换到 1920×1080 / 30 FPS / 4 Mbps，Transfer status 为 ready，SPS 为
`42c028`（Level 4.0）。该 Chrome 的默认 H264 接收 codec 列表都声明 Level 3.1，
服务端拒绝超过 offer 接收 Level 的流，返回：

```
no compatible H264 packetization-mode=1/profile/level in offer; stream=42c028
```

证据：`_output/pr10-chrome-acceptance/chrome-1080p.json`，包括真实浏览器 capabilities
和失败页面状态。**这不是仅 Firefox 的问题，当前不能宣称 Chrome 1080P 已支持/已验收。**
也不能仅凭默认 offer 就断言 Chrome 的实际解码器绝对不能解码 1080P；当前确认的是该信令
流程未协商成功。仍需解决符合实际接收能力与协议约束的协商方式，再执行解码/画面验收。
本轮没有改写 SDP Level、伪造 SPS 或跳过 Profile/Level 检查来制造通过结果。

Edge 本机未安装，**720P / 1080P 均未执行**；不能用 Chrome 结果代替 Edge。

### 联合路径：后端与 Chrome 通过，可见 Qt/VLC 验收仍未完成

在新的独立有界设备会话 `_output/pr10-chrome-parallel/` 中，同时运行：

- 同一个 Native Camera 与现有 libx264。
- Chrome 有界面 720P 实播，重复三轮连接/重连/刷新。
- Qt `cockpit-ui`，通过现有 HMI 控制 RPC 切到 Camera view，进程持续运行。
- GStreamer RTSP 接收端，完成 900 个 H.264 buffer 接收。
- Recording session 和手动事件 MP4，结束后使用 Jetson 解码器解码到 EOS。

该轮 Chrome 的 decoded 帧数分别为 **9 -> 310、11 -> 311、2 -> 303**，三轮均为
1280×720，观测到的 packetsLost / framesDropped 为 0。MP4 被识别为 1280×720、30 FPS，
可完成解码。这里仍然**不把 GStreamer RTSP 接收替代 VLC RTSP 播放验收**。

证据：`_output/pr10-chrome-parallel/chrome-720p.json`、`chrome-720p-*.png`、
`camera-status.json`、`recording-trigger.log`、`recording-stop.log`、`rtsp.log`、
`mp4-discover.log`、`mp4-decode.log` 及本地 session 中的 MP4。

当时记录的外部环境情况如下（Qt 现已 Out of Scope，不再作为阻塞）：

1. Jetson 桌面处于锁屏状态。Qt 窗口存在且 Camera view RPC 成功，但桌面截图实际是锁屏，
   不能当成 Qt 相机画面通过；没有尝试解锁或规避锁屏。相关截图不进入版本控制。
2. 本机原本未安装 VLC。本轮仅从 Ubuntu 22.04 官方包准备了用户目录临时测试运行环境，
   其 3.0.16 构建参数明确为 `--disable-live555`。实际连接本服务 RTSP 失败，日志为
   `no access modules matched` / `only real/helix rtsp servers supported`。
   这说明该客户端测试环境不具备本次标准 RTSP 播放条件，不是证明 Transfer RTSP 失败。
   没有为了测试重写 RTSP 或引入替代编码/服务；需要具有标准 RTSP 支持的 VLC 环境继续验收。

VLC 诊断保存在 `_output/pr10-chrome-acceptance/vlc-rtsp.log`；不要把其客户端错误
归为已通过的联合测试。另一次较早的联合脚本遭遇测试宿主自己的 10 分钟有界会话结束，
不计为产品通过/失败证据；上面通过的后端/Chrome联合结果来自随后新建的完整会话。

### 本轮结论与 UI 顺序

- Chrome 720P 的真实浏览器解码、页面截图、断开重连、刷新已经补上证据。
- 该阶段 Chrome 1080P、Edge、VLC 可见联合画面尚未关闭；Qt 现已 Out of Scope。
- 当时按 **先 P0 验收、后 P1 UI** 的顺序暂缓 UI；本轮 UI 已实现，见第 7 节。保留
  `video` / `connect` / `disconnect` / `status` DOM ID 或页面 `current` 会话变量。
- 后续 UI 仍使用现有原生 HTML/CSS/JS、getStats 和 status API；没有新增后台功能、PR-11
  或监控/调度平台。当前不能宣布 PR-10 全部验收完成。


## 7. `eb46f7b` 后续：合法接收能力、Web 控制台与三路验收

本节记录本轮工作区修改，**尚未提交**。Qt/HMI 已明确 Out of Scope，下面所有实际测试
均没有启动 Qt；源代码和构建任务也不修改 HMI。

### H.264 能力协商处理

按 RFC 6184 §8.1、§8.2.2 与 Table 6 补齐 `SelectH264Video()`：

- `max-recv-level` 按 **4 位十六进制 profile-iop + level_idc** 读取，不误当作三字节
  profile-level-id 或十进制。若声明该参数，其最高接收 Level 必须高于默认 Level。
- 处理 Level 1b 的特殊排序，不能只比较 level_idc 的整数大小。
- 区分 recvonly 与 sendrecv：单向接收按声明的最高接收能力判断；Table 6 指定单向模式
  忽略 level-asymmetry-allowed。双向模式才应用对称/非对称 Level 规则。
- 按 payload type 分别检查 profile、packetization-mode、max-recv-level、max-fs、max-mbps；
  非法/冲突参数只排除该 payload，仍可选择另一个兼容的 H264 payload。
- max-fs/max-mbps 可以扩展部分能力，但不能凭它们假定更高完整 Level 的 DPB/CPB/码率能力。
  当前 Transfer 没有足以证明所有其他 Level 条件的码流约束元数据，因此保守拒绝这种推断，
  不把少量参数拼成一个虚假的 Level 4.0 接收能力。
- 缺省 profile-level-id 按 RFC 的 Baseline Level 1 推断，不再假定为 720P Level 3.1。
- Answer 描述真实 SPS 的 profile/Level，不改写 SPS；不将接收端能力参数复制成发送端属性。
- 对不兼容客户端给出明确错误，提示使用兼容接收端或重新以 720P 启动 Camera；不自动
  操作 Camera、不新增编码器、不做转码，原有 720P、IDR、Generation 和分发均保留。

新增 `h264_negotiation_test` 覆盖上述规则。真实编码流集成测试中，受控 C++ 接收端明确
声明“默认 Level 3.1 + max-recv-level Level 4.0”，随后实际解码 **1920×1080**。
这验证合法扩展的路径，不代表普通浏览器已经声明或获得了这种能力。

### Chrome / Firefox 的原始能力与实测限制

现有 WebDriver 工具现在保存每轮**原始 Offer/Answer**，以及失败后的原始 Offer、source
status 和 getCapabilities。它仅记录，不修改浏览器 SDP。修正了 HTTP 200 的页面自定义
`{error: ...}` 被误当成 WebDriver 协议错误的问题，确保协商失败也能保存原始证据。
SDP 包含局域网候选地址与临时 ICE 凭据，全部保存在被 git 忽略的 `_output/`，不要上传。

| 接收端 | 默认 H264 声明 | 本轮结果 |
| --- | --- | --- |
| Chrome 157.0.8103.38 | H264 payload 的最高完整 Level 为 3.1，无 max-recv-level/max-fs/max-mbps 扩展 | 新 UI 的 720P 实播、重连、刷新通过；1080P 明确拒绝 |
| Firefox 153.0.4 + OpenH264 2.6.0 | H264 payload 为 Level 3.1，无更高完整 Level 声明 | 新 UI 的 720P headless 实际解码、重连、刷新通过；1080P 明确拒绝 |
| Edge | 当前设备没有 Edge 测试环境 | **未执行**，需要外部 Edge/EdgeDriver；不使用 Chrome 结果代替 |
| 受控 libdatachannel/libavcodec 接收端 | 默认 3.1，并明确声明 max-recv-level 4.0 | 真正的 1080P RTP 接收/解码通过 |

Firefox 的 VP8/VP9 payload 中即使出现 max-fs，也不能借给 H264 payload 使用。
默认浏览器仍不支持本流程的 1080P 协商，不通过篡改 Level 或跳过检查强行通过。
“1080P 问题已处理”在本轮表示规则与合法接收路径已修正、默认浏览器限制已明确记录，
**不表示 Chrome/Firefox 1080P 实播已通过**。

原始数据与汇总：

```
_output/pr10-final/device/chrome-720p.json
_output/pr10-final/device/firefox-720p.json
_output/pr10-final/device/*-initial-offer.sdp
_output/pr10-final/device/*-initial-answer.sdp
_output/pr10-final/1080-device/chrome.json
_output/pr10-final/1080-device/chrome-failure-offer.sdp
_output/pr10-final/1080-device/firefox.json
_output/pr10-final/1080-device/firefox-failure-offer.sdp
_output/pr10-final/offer-summary.json
```

### AIPC 风格原生 Web 控制台

已完成浅色工业控制台：侧边导航、运行概览、视频主面板、连接诊断、兼容性提示。
只使用原生 HTML/CSS/JavaScript 与原有接口，没有 Vue/React、外部 CDN、指标服务或新后台。
保留 `video`、`connect`、`disconnect`、`status` 和页面 `current` 绑定。

- 真实分辨率来自浏览器 videoWidth/videoHeight。
- 解码 FPS 与接收码率来自 getStats 计数差与采样间隔；丢包/丢帧为实际累计计数。
- Stream Generation、输入新鲜度、等待参数集/IDR/停流状态来自现有 status API。
- Peer/ICE、Session ID、浏览器 H264 声明及流 Profile/Level 可分别查看。
- 没有提供、尚未收到或已断开的指标显示 **—**，不使用配置 FPS/码率冒充观测。
- 指标与请求结果检查 Session 身份，避免旧连接更新新连接页面；断开清理 Peer 采样，
  空闲状态只保留后端状态更新，pagehide 清理全部计时器。

新 UI 已通过真实 Chrome 720P 三轮实播回归、Firefox 720P 解码回归，以及桌面/窄屏测试。
实际 viewport 宽度分别为 **1210 px / 500 px**，documentWidth 均不超过 viewport。
检查到实际 1280×720、约 30 解码 FPS、约 1.6～1.8 Mbps 接收码率及真实丢包/丢帧计数；
这些数字是该次现场样本，不是写入页面的固定值。

证据：`_output/pr10-final/device/console-desktop.json`、`console-mobile.json` 及 PNG。
可选 `--window-width/--window-height` 用于同一个验收脚本检查不同窗口尺寸，实际 viewport
仍记录在报告中，不将请求窗口尺寸当作实测 viewport。

### RTSP + WebRTC + 事件 MP4

本轮没有启动 Qt。真实 Native Camera 使用一份 libx264 H264 编码，同时：

1. Chrome 新控制台持续播放并执行三轮连接/重连/刷新；
2. GStreamer RTSP 接收端接收 900 个 buffer；
3. Recording 手动事件录像，完成 MP4 封装、发现和解码到 EOS。

这部分通过，但仍只是“Chrome + RTSP 协议接收 + MP4”，**不冒充标准 VLC 实播**。
另外用 Google Chrome 原生 MP4 播放器打开生成的事件录像：1280×720，时长约 29.83 秒，
播放时间 **0.0267 -> 5.0397 秒**，显示帧数 **4 -> 148**，未暂停、无 video error；
补上了实际播放器播放证据，而不是只有解码器测试。

证据在 `_output/pr10-final/device/`：`chrome-parallel.json`、`recording-trigger.log`、
`recording-stop.log`、`rtsp.log`、`mp4-discover.log`、`mp4-decode.log`、`mp4-player.json`
和播放器截图。不要把 MP4 播放结果当作 VLC RTSP 成功。

标准 VLC RTSP 与 720P/1080P 的完整三路可见联合验收仍需外部电脑提供证据。设备上的临时
Ubuntu VLC 包明确禁用了 Live555；不为补证而重写服务端或换编码器。本轮查询的官方 VLC
Snap arm64 渠道也没有可用构建，不能把 amd64 客户端冒充本机测试。

### 当前收尾清单

- [x] 1080P SDP 接收能力规则已处理，合法 max-recv-level 路径及浏览器限制有测试/记录。
- [ ] Chrome / Edge 全部实际播放和重连完成：Chrome 720P 已通过，Edge 仍待外部环境。
- [ ] 标准 VLC RTSP 实际播放通过。
- [ ] 浏览器 + 标准 VLC RTSP + 事件 MP4 完整联合验收通过。
- [x] AIPC 风格原生网页完成，旧通信流程与测试接口保留。
- [x] 自动回归、文档和当前已知限制整理。

**Qt / HMI：Out of Scope，不作为未完成项目。**
不能因为 UI 和合法接收能力处理已完成就关闭剩余外部验收项；不新增 PR-11。


本轮相关自动回归 **15/15 通过**（`_output/pr10-final/delivery-regression.log`）；
验收脚本的 Python 单元测试 11 项通过，前端 Node 测试覆盖原有连接取消/清理与真实计数差指标。
`git diff --check` 与模块边界检查通过。本轮没有开展长稳/性能压测。

测试启动的 Camera、Transfer、Recording 与 WebDriver 均已停止。代码和文档尚未提交。
截图、原始 SDP、JSON、MP4、下载的测试客户端/参考资料均留在 `_output/`，不进入提交。
