# Camera V2 PR-10：原生 C++ H.264 WebRTC

## 范围与数据路径

```
Native Camera -> ISP / DMA-BUF / VIC -> 原有 libx264（只编码一次）
  -> CRP1 / loopback UDP 8556 -> Transfer H264Receiver
       -> 独立有界队列 -> 原有 GStreamer RTSP Server
       -> 每 Peer 独立有界队列 -> libdatachannel H264 Track -> 浏览器
  -> 原有 Recording / loopback UDP 8557 -> MP4（不改）
```

不引入 GStreamer WebRTC、Rust、WebSocket、音频、DataChannel、TURN 或第二个编码器。
`transfer/video/H264Packet` 是 Transfer 的协议值类型，不依赖 Camera 的私有实现头文件；
接收 CRP1 后才构造，字段语义与已有 Camera H264Packet 一致。

## 固定依赖与 ARM64 构建

libdatachannel 固定 **v0.23.3**，提交 **d9391849bdb183854af7d4d92cae8f6a918d7a40**。
依赖在准备阶段下载、编译、安装；项目 CMake 使用 `find_package(... EXACT REQUIRED)`，
不使用 FetchContent，不在配置、编译或运行时联网下载。

Ubuntu 22.04 / Jetson 的系统开发依赖（已有的包可重复执行）：

```bash
sudo apt update
sudo apt install -y git cmake build-essential pkg-config libssl-dev \
  libsoup2.4-dev libjson-glib-dev \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstrtspserver-1.0-dev \
  gstreamer1.0-plugins-good gstreamer1.0-plugins-bad
```

不需要 `webrtcbin`、`gstreamer1.0-nice` 或 Rust。libsoup 仅用于 HTTP，JSON-GLib 用于信令 JSON。
下面命令只用于**显式准备依赖**，也可把完整源码与子模块离线拷贝到目标机：

```bash
DEPS="$PWD/_output/deps"
mkdir -p "$DEPS"
git clone --branch v0.23.3 --depth 1 --recurse-submodules --shallow-submodules \
  https://github.com/paullouisageneau/libdatachannel.git "$DEPS/libdatachannel-src"
test "$(git -C "$DEPS/libdatachannel-src" rev-parse HEAD)" = \
  d9391849bdb183854af7d4d92cae8f6a918d7a40
cmake -S "$DEPS/libdatachannel-src" -B "$DEPS/libdatachannel-build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$DEPS/libdatachannel-0.23.3" \
  -DNO_EXAMPLES=ON -DNO_TESTS=ON -DNO_WEBSOCKET=ON -DNO_MEDIA=OFF \
  -DBUILD_SHARED_LIBS=ON
cmake --build "$DEPS/libdatachannel-build" -j4
cmake --install "$DEPS/libdatachannel-build"

cmake -S . -B _output/build/pr10 \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DCOCKPIT_ENABLE_WEBRTC=ON \
  -DCMAKE_PREFIX_PATH="$DEPS/libdatachannel-0.23.3"
cmake --build _output/build/pr10 -j4
ctest --test-dir _output/build/pr10 \
  -R 'transfer_h264_receiver|webrtc_session|system_config|module_boundary' \
  --output-on-failure
```

默认 `COCKPIT_ENABLE_WEBRTC=OFF`，未安装依赖的现有构建仍可使用 RTSP；若此时配置启用
WebRTC，Transfer 明确报错，不会假装已启动。安装包会将 libdatachannel 以其 SONAME
放入 `lib/`，沿用模块 `$ORIGIN/../..` 的 RPATH。

## 运行配置

合并到现有配置的 `services.transfer`，不要覆盖其他服务：

```yaml
services:
  transfer:
    webrtc_enabled: true
    webrtc_address: 127.0.0.1 # 本机开发；局域网请显式改为 Jetson 的 LAN IPv4 地址
    webrtc_http_port: 8080
    webrtc_udp_port_begin: 50000
    webrtc_udp_port_end: 50010
    webrtc_max_clients: 1
    rtsp_enabled: true
    rtsp_address: 0.0.0.0
    rtsp_port: 8554
    rtsp_path: /camera
    rtsp_udp_port: 8556
    rtsp_fps: 30
```

`rtsp_udp_port` / `rtsp_fps` 为兼容旧配置保留名称，实际也是 WebRTC 的共享输入端口与
编码时间基。即便关闭 RTSP，WebRTC 仍从这个端口接收。FPS 必须与 Camera H.264 编码 FPS
一致（PTS 为编码器帧 tick，不是微秒）。Recording 8557 不变。

```bash
_output/build/pr10/bin/cockpit-navigator --config configs/development.yaml
_output/build/pr10/bin/camera-ctl --start --device nvargus://0 \
  --width 1920 --height 1080 --fps 30 \
  --h264-width 1280 --h264-height 720 --h264-bitrate 2000000 --h264-gop 30 \
  --h264-udp-host 127.0.0.1 --h264-udp-port 8556
```

浏览器打开 `http://<jetson-lan-ip>:8080/`，点击“连接 / 重连”。同时可在 VLC 打开
`rtsp://<jetson-lan-ip>:8554/camera`。页面会显示实际解码分辨率，而不是仅显示配置值。
1080P 验收时将 Camera H.264 宽高改为 1920×1080，并重新建立会话。

开发服务没有认证或 HTTPS，只可监听受信局域网/本机，不直接暴露公网；防火墙仅允许
受信客户端访问 TCP HTTP 端口与 UDP 媒体范围。媒体采用 Host ICE，无 STUN/TURN；
信令绑定地址不限制 ICE 使用的其他本地网卡，网卡访问限制由系统防火墙负责。
浏览器必须从服务自身页面访问；跨 Origin 请求被拒绝。反向代理需保持 Host/Origin 一致。

## HTTP 协议

- `GET /`：内嵌原生 HTML/JavaScript，无源码目录或网络 CDN 依赖。
- `GET /api/v1/webrtc/status`：视频可用性、generation、SPS profile-level-id、会话状态及
  `waiting_keyframe`。两秒未收到有效视频后视为停流。
- `POST /api/v1/webrtc/sessions`：`Content-Type: application/json`，body 为
  `{"type":"offer","sdp":"..."}`。浏览器先等 ICE gathering complete；服务器也等待收集完成，
  返回 `201 {"id":"...","type":"answer","sdp":"..."}`。
- `DELETE /api/v1/webrtc/sessions/{id}`：成功返回 204，不存在返回 404。

只接受一个可接收视频的 m-line（recvonly / sendrecv）。没有音频和 DataChannel。
请求体最多 64 KiB；未就绪返回 503、容量满返回 409、不兼容 SDP/profile 返回 400、
错误 Content-Type 返回 415、跨源返回 403、过大请求返回 413。ICE 收集超时十秒；
未连接十五秒、断连三秒、停流或 generation 改变都会清理 Peer。客户端掉线而没有 DELETE
也不会永久占用唯一的名额。参数 `webrtc_max_clients` 可设 1～4，默认验收只用一个。

## 媒体恢复规则

- CRP1：校验 magic、分片数/索引、PTS/flags 一致性、重复包、包大小（8 MiB）和 500 ms
  分片时限；丢包会使下游等待 IDR。
- 同 PTS 的连续编码包合并；下一 PTS 或 10 ms 输入空隙结束当前 AU（CRP1 无独立 AU
  end 标记，适配现有编码输出，不添加新协议）。
- 检查实际 NAL type，而非仅信任 CRP1 keyframe 位；缓存当前 SPS/PPS，IDR 前补参数集。
  Annex-B 统一四字节起始码；Packetizer 使用 `Separator::StartSequence`。
- 每订阅者最多 8 个 AU / 16 MiB，且队列满时丢掉整个待发 GOP，下一 IDR 恢复；
  接收线程不执行媒体发送。每 Peer 有独立 packetizer、SSRC、序号、时间戳和 RTCP 状态。
- SPS/PPS 改变、PTS 回退、输入恢复均创建新 generation，不把旧参数集带入新编码器。
  WebRTC 清理旧 Peer，用户点击重连；第一版不做后台自动无限重试。
- 90 kHz RTP 时间戳按 `PTS / FPS` 映射，每 Peer 第一帧建立独立原点；队列丢帧不重置
  该 RTP 原点。MTU 1200、NAL 分片上限 1000，SR/NACK 缓存上限 512 个 RTP 包。
  新 Track 打开、PLI/FIR、WebRTC 队列溢出和发送失败会异步请求 Camera IDR。
  复用 CameraControl gRPC，不新增服务/编码器；请求合并且限频 250 ms，失败仍可等周期 IDR。

## 自动测试与人工验收边界

`transfer_h264_receiver_test`：乱序/重复分片、元数据不一致、分片上限、缺包、混合 Annex-B
起始码、NAL keyframe 校验、generation、慢消费者队列溢出/IDR 恢复。

`webrtc_session_test`：HTTP/错误码、同源、请求大小、单客户端限制、本机非 Trickle SDP、
Host ICE/DTLS、SPS/PPS/IDR RTP、90 kHz 时间戳、PLI、重连、generation 清理与服务重启。
若检测到现有 libx264/FFmpeg 与 RTSP 开发依赖，还会生成真实 720P / 1080P 测试图码流，
经 Camera 现有 CRP1 UDP publisher -> 共享输入 -> RTSP/WebRTC 并行，WebRTC 接收端用
libavcodec 真正解码并检查尺寸。它不是 Chrome/Edge 验收，也不证明 Native Camera/Qt/MP4
真机并行已通过。

人工验收仍须记录以下结果，不能用自动协议测试代替：

- [ ] Chrome 与 Edge 实际播放 720P（页面实际尺寸、可见连续画面）。
- [ ] Chrome/Edge 实际播放 1080P。
- [ ] Qt 本地 Native Camera 预览与浏览器并行。
- [ ] VLC RTSP 与浏览器并行、编码器仍只有一份。
- [ ] 手动事件 MP4 录像期间浏览器持续播放；录像文件可播放。
- [ ] 浏览器刷新、断开后重连恢复。
- [ ] Camera 停止/重启清理旧会话，恢复后新会话可播放。

本 PR 后只修上述基本功能的阻塞问题，不增加 PR-11、多媒体模块或性能监控平台。

## 参考实现

- AIPC：`aipc-daemon/src/webrtc.rs` 的 generation / session / IDR 恢复思路；
  不复制其 Rust、音频、AI 通道和 fallback 框架。
- libdatachannel **v0.23.3**：`examples/streamer/main.cpp` 的 H264RtpPacketizer、
  RtcpSrReporter、RtcpNackResponder、sendFrame；本项目输入为 Annex-B，不使用示例的
  length-prefixed NAL 分隔符。

## 本次开发验证记录（2026-11-09）

环境：Jetson Linux ARM64，GStreamer 1.20，libdatachannel 0.23.3。

- libdatachannel 本地编译/链接成功，Transfer 动态模块已构建；本机 Peer 的非 Trickle
  SDP / Host ICE / DTLS 连接成功。
- 相关 CTest **10/10 通过**（14.57 秒）：模块边界 2 项、Gateway、Recording integrity、
  GOP ring、H264 encoder、SystemConfig、Transfer receiver、WebRTC session、浏览器 JS。
- 真实 libx264 测试码流经现有 CRP1 UDP publisher，共享供 RTSP/WebRTC 消费；
  WebRTC 接收端 libavcodec 解码得到 **1280×720** 与 **1920×1080**。
- 会话测试还覆盖 NACK responder 对指定 RTP 序号的缓存重传、RTP MTU、SDP profile 拒绝、
  Camera 停流后已连接 Peer 的回收。NACK 缓存测试在 SRTP replay protection 之前执行，
  避免把重复 RTP 被接收端正确丢弃误判为发送失败。
- 原生浏览器 JS 的 Node 模拟测试覆盖连接中取消、旧 answer 返回后的 DELETE、重复连接、
  generation/status 会话消失、pagehide；**不是 Chrome/Edge 实播测试**。
- WebRTC OFF：RTSP 与 WebRTC stub 构建成功，receiver 测试通过。
- 新增 receiver 与 HTTP/Peer/Track 路径单独 ASan/UBSan 执行通过；第三方 libdatachannel
  为非插桩 Release 库，这不等同于全部依赖均通过 sanitizer 或长稳测试。
- Chrome/Edge、Native Camera + Qt + VLC + 事件 MP4 并行仍为上面的待人工验收项。

开发日志在未入版本控制的 `_output/pr10-final-tests.log`、
`_output/build/pr10/Testing/Temporary/LastTest.log` 和 `_output/pr10-sanitizers/`。


## 设备端后续验收

真实 Native Camera 的 720P/1080P 接收解码、停流/重启、RTSP/事件 MP4 并行记录，以及
在 Chrome/Edge 工作站执行实播验收的命令，见 [PR-10 设备验收记录](CameraV2-PR10-acceptance.md)。
该记录明确区分 C++ 解码、Qt 进程运行、播放器可见画面与真正浏览器播放，不替代人工验收。


## 主动 IDR 与分阶段就绪（P2 补齐）

- `CameraControl.RequestKeyframe(Empty)` 返回 `accepted/error`；accepted 仅表示请求入队或合并，
  不等于已经产生关键帧。停止、未编码或非 Native 后端会明确拒绝。
- 链路：Transfer 的异步 coalescing worker -> 现有 Camera gRPC -> CameraService ->
  NativeArgusPreviewSource -> H264Encoder。不会重启 Camera、重建编码器或增加第二份编码。
- 编码端用 FFmpeg libx264 的 `forced-idr=1` 配合下一次 Encode 的 I-picture 请求，
  保证产生 IDR 而非只有普通 I-slice；实际 NAL type 5、SPS/PPS 由测试检查。
- Transfer RPC 最多每 250 ms 一次，单次 deadline 150 ms；Stop 会取消正在等待的 RPC。
  编码端也最多每 250 ms 强制一次，期间请求保留为一个 pending 标志。两层都按单编码流合并，
  不随浏览器数增加而复制编码或积压请求。
- 新 Track onOpen、PLI/FIR、WebRTC 订阅队列溢出、RTP 背压/发送异常会触发请求。
  Camera 控制链不可用时记录错误并保留原有周期 IDR 恢复能力。
- Camera status 增加 `native_encoder_idr_requests`、`native_encoder_forced_idr_frames`。
  后者是强制提交的帧数；要证明码流真正输出 IDR，仍需检查 NAL/接收解码证据。
- HTTP status 增加 `stream_state`、`input_age_ms`、`idr_received`。阶段为 `no_input`、
  `waiting_parameter_sets`、`waiting_idr`、`ready`、`stalled`、`stopped`。
  `video_available` 需要当前输入未超时、SPS/PPS 和当前 generation 的 IDR 已观察到；
  不再仅凭收到参数集就报告就绪。这是诊断字段，不是新增调度状态机。
- WebRTC connected 与浏览器实际 framesDecoded 仍是独立阶段，不能用 `stream_state=ready`
  代替浏览器播放成功。

手动验证（先使用新构建重启 Camera 与 Transfer）：

```bash
_output/build/pr10/bin/camera-ctl --config YOUR_CONFIG --request-idr --output json
_output/build/pr10/bin/camera-ctl --config YOUR_CONFIG --status --output json
```

验证 PLI 的设备接收 probe（不要同时占用默认单 Peer 的浏览器）：

```bash
_output/build/pr10/bin/webrtc_live_probe \
  --base-url http://JETSON_IP:8080 --width 1280 --height 720 \
  --seconds 6 --pli-after-seconds 2 --output _output/webrtc-acceptance/pli.json
```

新增 `h264_idr_test`、`camera_keyframe_client_test`；现有 WebRTC 与队列测试补了 onOpen、
PLI、溢出后的主动请求断言。均不把 RPC accepted 或计数增长单独当成解码成功。
