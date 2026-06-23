# 变更记录 / Change Log

本文记录 cockpit-system 的每批实现改动。后续记录统一包含变更内容、设计决定和验证结果。

This file records every implementation batch for cockpit-system. Future entries include
changes, design decisions, and verification results.

## 2026-06-23 - HMI 命令交接边界 / HMI Command Handoff Boundary

### 变更内容 / Changed

- 新增 `HmiCommandProvider`，用于 open camera preview 和 play music 等用户可感知 HMI 命令。
- CockpitActionDispatcher 可选接入 HMI provider；未配置时返回 not_implemented。
- voice-interaction-service 接入 `LocalHmiCommandProvider`，当前只记录 handoff，不执行 Android/UI 动作。
- 测试覆盖 HMI 命令成功、未配置和失败路径。

### 设计决定 / Design Decisions

- C++ voice 模块不实现 Android 音乐播放器，也不直接操作 UI App。
- 播放音乐、打开摄像头预览等动作只作为 typed command 交给未来 Qt/Android/HMI bridge。
- 本地 provider 是调试占位实现，返回“已记录命令”，不声称真实 App 已打开。

### 验证结果 / Verification

- cockpit_action_dispatcher_test 覆盖 HMI handoff 行为。

## 2026-06-23 - 语音动作与录包边界 / Voice and Recording Boundary

### 变更内容 / Changed

- 从 voice intent/action 白名单移除 start/stop recording。
- mock assistant 不再把录包命令识别成用户语音动作。
- 文档明确录包、数据采集和研发记录属于 diagnostics/recording 边界，不属于 voice 主线。

### 设计决定 / Design Decisions

- voice-interaction-service 只处理用户语音交互。
- 录包、雷达/音视频研发采集、数据记录后续如果需要，应走独立 recording/diagnostics 边界和研发工具。

### 验证结果 / Verification

- 更新 mock voice 和 voice interaction 测试，确保录包短语不再触发 voice action。

## 2026-06-23 - 同进程事件队列 / In-process Event Queue

### 变更内容 / Changed

- 新增 `core/event/EventQueue<T>`，用于低频 typed event 的同进程投递。
- 支持 bounded capacity、TryPop、WaitPop、WaitPopFor、Close、Reset 和 drop_count。
- voice-interaction-service 使用 EventQueue 将 transcript 接收和 intent/action 处理解耦。
- voice gRPC status 和 voice-ctl 暴露 transcript event drop 指标。
- 新增 event CMake target 和 `event_queue_test`。

### 设计决定 / Design Decisions

- 该队列不是高频数据面，不替代 audio SPSC ring，也不承载 PCM/图像大 buffer。
- 当前只做同进程事件队列，不做跨进程 IPC、shared memory 或网络 message bus。
- gRPC transcript stream 线程只投递事件，voice worker 线程串行执行 assistant、action 和 TTS。

### 验证结果 / Verification

- `event_queue_test` 覆盖顺序、溢出、move-only 事件、等待、关闭和 reset。
- `voice_interaction_service_test` 覆盖异步 transcript 入队、响应发布和停止后拒绝入队。

## 2026-06-23 - znavigator 模块化参考固化 / znavigator Modularization Notes

### 变更内容 / Changed

- 补充 znavigator 目录结构到 cockpit-system 当前目录的映射关系。
- 明确 znavigator 更像运行时/模块编排器，不是自动驾驶算法栈模板。
- 记录 `library/*_entry`、`dl_api`、`zoe_*`、`transfer/restful` 对当前项目的参考价值。
- 补充 `ipc_connector`、`socketpair`、`ProtocolUnit` 和 `application.yaml` channel wiring 的实际阅读结论。
- 明确 core、modules、drivers、services、tools、proto、configs 的放置规则。
- 增加 cockpit-system 通信规则：gRPC 做控制/调试，高频数据使用本地队列、ring 或后续共享内存。
- 新增 runtime 通信策略文档，明确同线程、同进程、跨进程、跨机器的传输选择。
- 修正 modules/drivers README 中 audio、voice、alsa 已落地后的过期描述。

### 设计决定 / Design Decisions

- 继续参考 znavigator 的薄入口、小内部库、显式模块边界思路。
- 不切换到 xmake，也不照搬 `library/*_entry` 目录命名；当前服务边界用 `services/*` 表达。
- 当前阶段不做 `dlopen` 插件系统和通用 REST 控制面；先用静态服务、systemd、smoke 脚本和 typed gRPC。

### 验证结果 / Verification

- 本批为文档和结构规则更新，无需重新编译。

## 2026-06-23 - 真实车辆状态语音动作 / Real Vehicle Status Voice Action

### 变更内容 / Changed

- gateway 新增 GetLatestVehicleState unary RPC，并拒绝缺失或超过 2 秒的旧快照。
- 新增平台无关 VehicleStatusProvider 和 CockpitActionDispatcher。
- voice service 新增 GatewayVehicleStatusClient，query_vehicle_status 不再使用 mock action。
- 动作结果成为最终 response text，可直接播报实时车速、电量和 gear code。
- voice interaction 配置新增 gateway_address；其他动作继续返回 not_implemented。
- smoke 改为 audio、vehicle、gateway、voice 四服务并行联调。

### 设计决定 / Design Decisions

- voice 只访问 gateway 聚合接口，不直接依赖 vehicle-data-service 的底层 streaming API。
- 每种真实动作使用独立类型化 provider，不引入通用 shell 或任意 RPC 调用能力。
- 车辆 gear 尚无正式信号规范，因此播报 gear code，不猜测 P/R/N/D 语义。

### 验证结果 / Verification

- dispatcher 测试覆盖真实快照格式化、provider 失败、缺失和未实现动作。
- CTest 15/15 通过。
- 完整 smoke 返回实时车辆状态，action succeeded、speech accepted，TTS played=2/failed=0。

## 2026-06-23 - TTS 与扬声器输出 / TTS and Speaker Output

### 变更内容 / Changed

- 新增平台无关 SpeechSynthesizer、AudioPlayer 和 VoiceResponseSink 接口。
- 新增确定性 mock TTS，生成 16 kHz、mono、PCM16 提示音。
- audio-service 新增容量为 8 的异步 SpeechOutput 队列和 Speak(text) gRPC。
- 新增 ALSA AudioPlayer；audio-probe 支持 speak 并展示 TTS 指标。
- voice-interaction-service 通过 AudioSpeechClient 发送文本，不直接访问 ALSA 或传输 PCM。
- audio.proto 与 voice.proto 分离保留，分别表达真实播放指标和文本请求投递指标。

### 设计决定 / Design Decisions

- audio-service 独占麦克风和扬声器；voice-interaction-service 只负责对话与动作编排。
- TTS PCM 只存在于 audio-service 进程，跨服务仅传输 response text。
- 播放使用唯一 worker，停止时丢弃待播队列，只允许当前播放收尾。
- mock TTS 是可听测试提示音，不冒充真实语音合成。

### 验证结果 / Verification

- speech_output_test 覆盖 PCM 格式、后台播放、生命周期和非法依赖。
- CTest 14/14 通过。
- 完整 smoke 验证 Speak -> mock TTS -> ALSA null，queued=1、played=1、failed=0。

## 2026-06-23 - 受控语音动作分发 / Controlled Voice Action Dispatch

### 变更内容 / Changed

- 新增平台无关 ActionDispatcher 和 ActionExecutionResult。
- mock dispatcher 仅接受类型化白名单动作，非法枚举返回 rejected。
- voice response 新增 action status/message，区分意图识别和动作执行结果。
- service status 新增 attempted、succeeded、failed 动作指标，voice-ctl 同步展示。
- 缺少 dispatcher 时返回 not_implemented，不再让响应表现为动作已完成。

### 设计决定 / Design Decisions

- dispatcher 只接收 VoiceAction，不接收命令字符串，也不提供 shell 执行入口。
- 当前运行时使用 mock dispatcher；真实动作后续通过相机、媒体等用户可感知服务客户端实现。
- response_text 和 action execution 分离，便于后续 TTS 选择可靠的最终播报内容。

### 验证结果 / Verification

- 测试覆盖白名单成功、none、非法动作拒绝和 dispatcher 缺失路径。
- service 测试覆盖动作状态、成功/失败指标及既有有序 response 历史。
- CTest 13/13 通过；完整 smoke 通过并展示新增动作指标。

## 2026-06-23 - 语音交互服务 / Voice Interaction Service

### 变更内容 / Changed

- 新增 voice-interaction-service，订阅 audio-service transcript 并生成有序响应事件。
- 新增确定性 mock assistant，识别车辆状态、相机和音乐等白名单意图。
- 新增 VoiceInteractionControl gRPC status、调试处理和 response stream 接口。
- 新增 voice-ctl status/process/responses 调试工具和 systemd 单元。
- YAML 新增 voice interaction 上游地址、重连参数和 127.0.0.1:50053 监听地址。

### 设计决定 / Design Decisions

- 意图识别位于平台无关 modules/voice，服务负责生命周期、指标和 gRPC 边界。
- mock action 仅生成类型化动作，不执行 shell 或安全关键车辆控制。
- 默认 features.voice.enabled 为 false；smoke 验证禁用状态，正向意图路径由单元测试覆盖。
- response 历史容量为 32，仅用于短时重连恢复，不承担持久化。

### 验证结果 / Verification

- mock assistant 测试覆盖全部白名单意图和 unknown fallback。
- service 测试覆盖禁用状态、顺序响应、指标、历史和非法输入。
- CTest 13/13 通过；完整 smoke 包含 voice gRPC 启动、探活和状态查询。

## 2026-06-23 - Mock ASR 与转写事件 / Mock ASR and Transcript Events

### 变更内容 / Changed

- 新增 `modules/voice`、`SpeechRecognizer`、`SpeechTranscript` 和确定性 mock provider。
- `audio-service` 新增 ASR consumer thread，按顺序消费完成的 SpeechSegment 并排空停止队列。
- transcript 使用单调 ID 和容量 32 的有界历史，支持多个订阅者按 ID 顺序读取。
- `AudioControl.SubscribeTranscripts` 提供 text-only server stream，不传输原始 PCM。
- `audio-probe --transcripts --count N --timeout-ms N` 可观察转写事件。
- status 新增 ASR enabled、processed、published 和 error 指标。
- topic gRPC subscriber 在总 deadline 内自动续连，避免瞬时 UNAVAILABLE 中断 echo/hz。

### 设计决定 / Design Decisions

- ASR 与 ALSA/VAD 解耦，只依赖完成的 SpeechSegment；未来可替换 whisper.cpp/TensorRT。
- `features.voice.enabled` 控制 ASR worker，默认关闭；当前 provider 仅支持 mock。
- transcript 历史只处理短暂订阅断线，不代替 SQLite 或持久化事件存储。
- 语音段队列只有 ASR 一个 consumer，启用 ASR 后禁止外部抢读。

### 验证结果 / Verification

- mock recognizer 测试覆盖确定性结果和空段错误。
- ASR pipeline 测试覆盖 segment→recognizer→ordered transcript 与指标。
- CTest 11/11 通过；完整 smoke 通过，默认配置正确显示 ASR disabled。

## 2026-06-22 - 语音段聚合 / Speech Segment Aggregation

### 变更内容 / Changed

- 新增平台无关 `SpeechSegment` 与 `SpeechSegmenter`，将逐帧 VAD 结果聚合为连续 PCM16。
- 支持 100 ms pre-roll、silence endpoint、Stop flush、discontinuity 中断和 15 秒上限。
- `audio-service` 新增容量为 8 的 speech segment SPSC 队列及完成、截断、丢弃指标。
- gRPC status 与 `audio-probe` 增加 segment 数量、截断、队列丢弃和最近时长。
- YAML 新增 `speech_segment.pre_roll_ms/max_segment_ms`，并校验 frame 对齐。
- AudioControl client 启用 wait-for-ready；smoke 中 gateway 改为脚本主动停止，消除有限样本竞态。

### 设计决定 / Design Decisions

- AudioFrame ring 仍由 VAD worker 独占；下一批 ASR 只消费完成的 SpeechSegment 队列。
- pre-roll 保留 VAD debounce 前的起音，silence hangover 已包含在 segment 尾部。
- 超长语音分段并标记 truncated；采集 discontinuity 立即封段并标记 discontinuous。
- 原始 PCM 与 SpeechSegment 均不经过 gRPC，控制面只暴露指标。

### 验证结果 / Verification

- `speech_segmenter_test` 覆盖 pre-roll、endpoint、flush、truncation 和 discontinuity。
- `audio_service_test` 验证 segment queue 与 service 指标；CTest 9/9 通过。
- 完整 smoke 通过，null silence 不产生语音段，既有车辆 topic echo/hz 保持通过。

## 2026-06-22 - 本地语音活动检测 / Local Voice Activity Detection

### 变更内容 / Changed

- 新增平台无关 `VoiceActivityDetector` 接口和 dependency-free `EnergyVad`。
- 能量 VAD 输出 RMS dBFS，并通过连续 speech frames 与 silence hangover 稳定状态转换。
- `audio-service` 新增唯一 VAD consumer thread，持续消费 SPSC ring 并统计事件。
- gRPC status 和 `audio-probe --status` 输出 VAD 状态、输入电平及处理指标。
- YAML 新增 VAD backend、阈值和帧窗口，并校验 backend、范围与正整数约束。

### 设计决定 / Design Decisions

- 当前使用能量 VAD 保证 WSL/Jetson 可直接运行，不提前引入 WebRTC 依赖。
- VAD 遇到 discontinuity 会重置 debounce，避免丢帧前后的音频被拼成一次语音。
- VAD worker 是 ring 的唯一 consumer；后续 ASR 从语音段事件接入，不直接抢读 ring。

### 验证结果 / Verification

- `energy_vad_test` 覆盖 dBFS、speech debounce、silence hangover 和 discontinuity reset。
- `audio_service_test` 验证 VAD worker 持续消费采集帧；CTest 8/8 通过。
- 完整 smoke 通过，ALSA `null` 被识别为 silence、-120 dBFS，VAD metrics 持续增长。

## 2026-06-22 - C++ 标准基线 / C++ Standard Baseline

- 全工程统一使用 C++17，不提供 C++20/23 构建开关。
- README、架构和配置文档统一使用 C++17 表述。

## 2026-06-22 - 音频采集服务 / Audio Capture Service

### 变更内容 / Changed

- 新增 `audio-service`，统一持有 ALSA capture source 和 `AudioCaptureStream`。
- `AudioControl` gRPC 支持 start、stop、status 和实时 metrics，PCM 不通过 gRPC 传输。
- `audio-probe` 新增 `--start/--stop/--status` 远程控制命令。
- Start RPC 等待设备打开结果；本地 PCM 保留为后续 VAD/ASR 的进程内数据面。
- 新增 service 生命周期单元测试、ALSA `null` gRPC smoke 和 systemd 单元。

### 设计决定 / Design Decisions

- gRPC 是控制面，本地 SPSC ring 是数据面；唯一 consumer 留给后续真实 VAD/ASR。
- `audio-service` 独占麦克风设备；UI 和语音模块不直接访问 ALSA。
- 当前批次只完成采集控制，扬声器播放控制仍为后续能力。

### 验证结果 / Verification

- CTest 7/7 通过；完整 smoke 验证 gRPC start/running/status/stop 和既有车辆链路。

## 2026-06-22 - 音频采集流引擎 / Audio Capture Stream Engine

### 变更内容 / Changed

- 新增平台无关 `AudioCaptureSource` 接口和单采集线程 `AudioCaptureStream`。
- ALSA capture 改为 non-blocking + poll，返回 ok/timeout/xrun/stopped/device error 状态。
- 新增 `AlsaCaptureSource` 适配器，并严格校验 16 kHz、mono、PCM16、20 ms 语音格式。
- 支持 partial read 拼帧、XRUN 恢复标记、ring overflow 丢帧计数和错误状态查询。
- `audio-probe --capture` 迁移到 poll/status API，不再使用阻塞 bool 读取接口。

### 设计决定 / Design Decisions

- `modules/audio` 保持平台无关；ALSA poll、设备句柄和 recover 仅位于 `drivers/alsa`。
- capture thread 是唯一 producer，VAD/ASR consumer 不反向阻塞设备读取。
- gRPC 后续只承担 start/stop/status 控制面，原始 PCM 留在进程内数据面。
- stream 停止后可安全清空 ring；故障信息在 `Stop()` 前可查询，停止后状态归一为 stopped。

### 验证结果 / Verification

- audio、alsa_audio、audio-probe 和新增 stream test 编译通过。
- fake source 覆盖 timeout、XRUN、两个半帧拼接、flags、metrics、故障和停止关闭。
- CTest 6/6 通过；ALSA `null` 经 poll 路径成功采集 16000 帧并写入 PCM16 WAV。

## 2026-06-22 - 实时音频帧与 SPSC / Real-time Audio Frames and SPSC

### 变更内容 / Changed

- 新增不可变 `AudioFrame`，固定为 16 kHz、mono、PCM16、20 ms、320 samples。
- frame 包含 sequence、单调时钟时间戳和 discontinuity/xrun/drop flags。
- 新增固定容量 lock-free `SpscRingBuffer<T, Capacity>`，容量强制为 2 的幂。
- ring 满时拒绝新帧并累计 drop count，不修改 consumer 所有的 read index。
- 新增顺序、回绕、溢出、不可赋值约束和 5 万帧双线程并发测试。

### 设计决定 / Design Decisions

- 固定语音格式由类型常量表达，不在每帧重复保存 sample rate 和 channels。
- 无效帧不进入数据面；不连续通过 flags 和 metrics 表达。
- 320 个 PCM16 samples 直接按值传递，当前不引入复杂零拷贝内存池。
- ring 仅支持 single producer/single consumer，控制面不得直接访问槽位。

### 验证结果 / Verification

- audio target 和 `audio_frame_buffer_test` 编译通过，CTest 5/5 通过。
- 5 万帧 producer/consumer 并发测试连续运行 20 次，无乱序或数据损坏。
- 编译期确认当前目标的 index 和 drop metric atomics 为 lock-free。

## 2026-06-22 - ALSA 驱动与音频探针 / ALSA Driver and Audio Probe

### 变更内容 / Changed

- 新增 `drivers/alsa`，封装 PCM 设备枚举、格式协商、读写、xrun recover 和 RAII 关闭。
- 新增 `audio-probe --list/--capture/--play`，复用类型化 audio config 和 WAV 模块。
- ALSA period buffer 配置为约四个周期，并拒绝零进度 I/O，避免死循环。
- 音频 backend 当前严格校验为 ALSA，设备名不能为空。
- 默认 smoke 增加 ALSA 设备枚举。

### 设计决定 / Design Decisions

- ALSA 细节只存在于 `drivers/alsa`，WAV 和 PCM 领域模型继续留在 `modules/audio`。
- probe 按 period 分块处理并响应 runtime stop，不承担常驻服务职责。
- 真实麦克风/扬声器由 Jetson 实机验证；无硬件环境使用 ALSA `null` 验证软件链路。

### 验证结果 / Verification

- ALSA 1.2.6.1 driver 和 `audio-probe` 编译通过，CTest 4/4 通过。
- WSL 枚举到 `null` duplex PCM。
- null capture 录制 16000 帧，生成 PCM16 WAV；null playback 完整播放 16000 帧。
- 默认 smoke 包含 audio device list，并保持完整通过。

## 2026-06-22 - 音频核心与 WAV / Audio Core and WAV

### 变更内容 / Changed

- 新增 `modules/audio`，提供 PCM16 格式校验、帧大小和周期帧数计算。
- 新增 RIFF/WAVE PCM16 读写，支持单声道和多声道交错 samples。
- WAV 解析校验格式、完整帧、截断数据和 512 MiB 输入上限。
- Ubuntu 依赖清单增加 `libasound2-dev` 和 `alsa-utils`，为下一批 ALSA 接入准备。
- 新增 `audio_wav_test`，覆盖格式校验、往返、非法文件和缺失文件。

### 设计决定 / Design Decisions

- `modules/audio` 不依赖 ALSA、Qt 或 gRPC，保持平台无关。
- ALSA 设备句柄放入 `drivers/alsa`，服务生命周期放入 `services/audio-service`。
- 当前只支持语音链路需要的 PCM16，小步扩展格式，不预建复杂媒体框架。

### 验证结果 / Verification

- audio target 和 WAV test 编译通过。
- CTest 4/4 通过。

## 2026-06-22 - Qt UI 数据新鲜度 / Qt UI Data Freshness

### 变更内容 / Changed

- `VehicleStateModel` 新增独立 `fresh` 属性和 1.5 秒 stale timer。
- gRPC 已连接但车辆状态停止更新时，界面显示 STALE，不再把旧值标记为实时数据。
- transport 断开时立即停止 timer 并清除 fresh 状态。
- `ui_model` 拆为独立 target，新增 QtTest 覆盖初始、超时和断线行为。

### 验证结果 / Verification

- Qt UI 和 `vehicle_state_model_test` 编译通过。
- CTest 3/3 通过，fresh→stale 定时测试耗时约 80 ms。
- QML offscreen 加载无属性绑定错误，SIGTERM 后状态码 0。

## 2026-06-22 - Qt UI 一键联调 / Qt UI Demo Runner

### 变更内容 / Changed

- 新增 `scripts/run_cockpit_ui.sh`，一条命令启动车辆服务、网关和 Qt UI。
- 使用 gRPC topic discovery 探测网关就绪，不依赖固定启动延时。
- UI 退出或脚本收到信号后，统一停止并等待后台服务。
- 默认使用 mock 数据，可通过 `VEHICLE_SOURCE=socketcan` 切换 CAN 数据源。
- 网关上游 gRPC stream 增加停止监视，信号到达后主动取消阻塞式 `Read()`。

### 验证结果 / Verification

- offscreen 一键链路成功启动并连接 UI、gateway 和 vehicle-data-service。
- 单次 Ctrl+C 后 UI、gateway 和 vehicle-data-service 均完成清理，无残留进程。
- 网关在信号到达后约 20 ms 完成上游取消并退出，不再等待 10 秒 stream deadline。
- Codex 隔离网络中仍观察到 localhost gRPC reset；无代理对照未改善，需在普通 WSL
  终端继续确认宿主环境表现。

## 2026-06-22 - Qt UI 信号退出 / Qt UI Signal Shutdown

### 变更内容 / Changed

- `cockpit-ui` 增加 Qt event loop 与 `ServiceRuntime::ShouldStop()` 的生命周期桥接。
- SIGINT/SIGTERM 到达后由主线程调用 `QCoreApplication::quit()`。
- `aboutToQuit` 继续负责取消 gRPC context 并 join worker thread。

### 验证结果 / Verification

- Qt6 QML 页面已在 offscreen 平台成功加载。
- `cockpit-ui` 重新构建通过；单次 SIGTERM 后打印 stopped 并以状态码 0 退出。

## 2026-06-21 - Qt 6 车机界面基线 / Qt 6 Cockpit UI Baseline

### 变更内容 / Changed

- `cockpit-ui` 从 Qt 5.15 构建声明迁移到 Qt 6。
- QML import 移除 `2.15/1.15` 版本锁，使用 Qt 6 模块解析。
- Ubuntu 依赖替换为 Qt 6 base、declarative 和对应 QML runtime modules。
- UI 通过 C++ `GatewayClient` 订阅 CockpitEvent，通过 `VehicleStateModel` 更新 QML。

### 设计决定 / Design Decisions

- 新项目不保留 Qt5 fallback，减少双版本兼容成本。
- Ubuntu 22.04 的 Qt 6.2.4 只作为 WSL 编译下限。
- Jetson 发布环境固定 Qt 6.8 LTS 工具链，并通过 `CMAKE_PREFIX_PATH` 显式选择。
- Qt UI 保持可选 target，headless 服务构建不依赖图形栈。

### 验证结果 / Verification

- Ubuntu 22.04 软件源确认可提供 Qt 6.2.4 base、declarative 和 QML runtime modules。
- Qt 6.2 使用通用 `add_executable`、`AUTOMOC` 和 `AUTORCC`，不依赖较新 Qt 的
  `qt_standard_project_setup`。
- QML runtime 依赖补充 `QtQuick.Templates` 与 `QtQuick.Window` 模块。
- headless 默认构建和 CTest 2/2 通过，`BUILD_COCKPIT_UI=ON` 编译链接通过。
- offscreen 启动已进入 QML engine；补装新增的 Templates/Window runtime 后继续验证页面加载。

## 2026-06-21 - gRPC topic 发现 / gRPC Topic Discovery

### 变更内容 / Changed

- Gateway 协议新增 `TopicMetadata`、`ListTopics` 和 `GetTopicInfo`。
- 网关公开 `/vehicle/state` 的消息类型、来源和读写能力。
- `topic list/info --backend grpc` 可查询运行中网关，不再依赖文件 registry。
- 默认 smoke 新增 gRPC topic 发现和类型信息验证。

### 设计决定 / Design Decisions

- 当前元数据由网关显式维护，不提前引入动态注册中心。
- `/vehicle/state` 标记为可订阅、不可发布，避免调试工具伪造车辆状态。
- gRPC 连接和错误映射集中在 `TopicGrpcDiscovery`，命令文件只处理展示。

### 验证结果 / Verification

- proto 重新生成、增量构建和 CTest 2/2 通过。
- 完整 smoke 中 `topic list` 返回 `/vehicle/state`，`topic info` 返回类型、来源和读写能力。
- discovery、echo、hz 以及上游断线自动重连链路均验证通过。

## 2026-06-21 - 网关事件流与 topic gRPC / Gateway Events and topic gRPC

### 变更内容 / Changed

- `cockpit-gateway-service` 实现 `SubscribeCockpitEvents` server-streaming 服务。
- VehicleState 上游消息转换为 CockpitEvent，并以“最新值 + 版本号”方式转发。
- `topic echo` 和 `topic hz` 新增共享 gRPC 订阅后端，当前支持 `/vehicle/state`。
- 新增 `--backend grpc`、`--count` 和 `--max-hz` 参数。
- 默认 smoke 覆盖 vehicle → gateway → topic 的双段 gRPC 数据流。

### 设计决定 / Design Decisions

- 文件后端继续保留，用于离线调试；gRPC 后端用于运行中服务订阅。
- 网关和 topic 使用生成的 protobuf 类型，不增加无类型内部消息总线。
- 当前只公开已有 schema 的 `/vehicle/state`，未知 topic 明确返回错误。
- gRPC 服务和客户端使用显式 Start/Stream/Shutdown 生命周期，不隐藏后台线程。

### 验证结果 / Verification

- 增量构建通过，CTest 2/2 通过。
- `topic echo` 通过网关连续收到 3 条 VehicleState CockpitEvent。
- `topic hz` 通过网关测得约 4.963 Hz，与 200 ms mock 发布周期一致。
- 完整 CAN/stdout、双段 gRPC、cloud placeholder 和文件 topic smoke 通过。

## 2026-06-21 - zcarcloud 配置架构参考 / zcarcloud Configuration Reference

### 变更内容 / Changed

- 审计 `z-car-cloud.yaml`、`CarCloud::InitEnv`、`ConfigManager`、listener runtime 和各 target 依赖。
- 新增 `docs/configuration.md`，确定当前项目的配置分区、类型归属和启动校验规则。
- Ubuntu 依赖增加 `libyaml-cpp-dev`，用于替换当前两层标量解析器。
- 新增不可变 `SystemConfig` 与 system、paths、logging、services、hardware、features、tools
  类型化子配置。
- `config.yaml` 按职责重新分区，全部服务和工具移除 dotted string key 读取。
- 日志 `mirror_stderr`、gRPC stream timeout/retry 参数开始实际生效。
- 删除重复且未使用的 `configs/logging.yaml`。

### 设计决定 / Design Decisions

- 采用 zcarcloud 的配置驱动、类型化转换和显式组件生命周期。
- 不复制全局 ConfigManager 单例、Zoo service locator 或 zelos 平台专用依赖。
- gRPC、CAN、MQTT 等组件读取类型化子配置，不再长期保留 dotted string key。

### 验证结果 / Verification

- yaml-cpp 0.7.0 配置库编译通过。
- `system_config_test` 验证真实配置加载和非法 gRPC 地址路径提示。
- CTest 2/2 通过，完整 gRPC/CAN/topic smoke 通过。

## 2026-06-21 - VehicleState gRPC 数据流 / VehicleState gRPC Stream

### 变更内容 / Changed

- 新增 `proto/CMakeLists.txt`，通过 `protoc` 和 `grpc_cpp_plugin` 生成 `contracts` target。
- proto package 统一为 `cockpit.proto.*`，避免生成类型与领域模型同名冲突。
- `vehicle-data-service` 新增同步 server-streaming 服务，将最新 VehicleState 推送给订阅者。
- `cockpit-gateway-service` 新增 streaming 客户端、10 秒限时重连和时间戳去重。
- 默认 smoke 改为双进程 gRPC 集成测试，自动启动和停止 vehicle 服务。

### 设计决定 / Design Decisions

- 生成代码只进入 `build/proto/generated`，不提交到仓库。
- 服务端只保存最新状态和版本号，不无限缓存过期车辆状态。
- 本机 gRPC 使用 insecure credentials；远程接口不得直接复用该安全配置。
- Vehicle gRPC 默认只监听 `127.0.0.1:50050`，不暴露到 Jetson 外部网卡。
- 本机 channel 显式关闭 HTTP proxy，避免系统代理干扰 localhost 服务通信。

### 验证结果 / Verification

- protobuf 3.12.4、gRPC 1.30.2 代码生成和编译通过。
- `cockpit_smoke_test` 通过。
- gateway 通过 `127.0.0.1:50050` 连续收到 3 条 VehicleState，完整 smoke 通过。

## 2026-06-21 - gRPC 构建前置 / gRPC Build Prerequisites

### 变更内容 / Changed

- 检查 WSL2 当前工具链，确认尚未安装 `protoc` 和 gRPC C++ 开发库。
- 确认 Ubuntu 22.04 apt 提供 protobuf 3.12.4 与 gRPC 1.30.2。
- `scripts/install_ubuntu_deps.sh` 增加 protobuf 编译器、gRPC 插件和 C++ 开发包。

### 设计决定 / Design Decisions

- 当前阶段使用 Ubuntu 系统包，暂不引入 Conan、vcpkg 或源码编译 gRPC。
- 依赖安装完成后再提交并验证 proto 生成和 gRPC 业务代码，避免未编译代码进入主链路。

### 验证结果 / Verification

- 依赖已在 WSL2 安装，`protoc`、`grpc_cpp_plugin` 和 pkg-config 检查通过。

## 2026-06-21 - SocketCAN 车辆数据链路 / SocketCAN Vehicle Data Path

### 变更内容 / Changed

- 新增平台无关 `VehicleCanCodec`，定义可测试的原型 `0x123` VehicleState 帧。
- `vehicle-data-service` 支持 `--source mock|socketcan`，并将业务循环从 `main.cc` 拆出。
- SocketCAN 接收支持 poll 超时、空闲退出、无关帧忽略和非法帧告警。
- `can-simulator` 改为通过相同 codec 生成车辆状态帧。
- 新增 `scripts/setup_vcan.sh` 与 `scripts/run_vcan_smoke.sh`。
- Ubuntu 依赖增加 `can-utils`、`iproute2` 和 `kmod`。

### 设计决定 / Design Decisions

- CAN 字节映射属于 `modules/vehicle`，Linux socket 访问继续留在 `drivers/socketcan`。
- `0x123` 是 WSL/Jetson 原型测试协议，不冒充旧项目或真实车辆 DBC。
- 正式 DBC 到位后替换 codec，不改变 SocketCAN 驱动和服务生命周期。

### 验证结果 / Verification

- 标准 `build/` 增量构建完成，`cockpit_smoke_test` 通过。
- codec 往返、无关帧忽略和短帧拒绝均有测试覆盖。
- 默认 mock/stdout `scripts/run_smoke.sh` 完整通过。
- 在 WSL2 交互终端运行 `scripts/run_vcan_smoke.sh`，成功创建并启用 `vcan0`。
- `can-simulator` 通过 SocketCAN 发送 3 帧，`vehicle-data-service` 完整接收并解码为
  12.0、15.5、19.0 km/h，对应挡位与 SOC 也一致。

## 2026-06-20 - common 职责复核 / Common Responsibility Review

### 变更内容 / Changed

- 抽样检查 `znavigator`、`safe_ota`、`zcarcloud` 以及云端前后端工程的分层方式。
- 确认 zelos 中的 `common` 主要保存组件内部共享的状态、协议结构和抽象接口，
  并不是跨项目基础设施的固定目录。
- 当前仓库不恢复顶层 `common`；对应职责由 `proto` 和具体 `modules` 承担。
- 删除兼容用 `core` 聚合 target，服务和工具改为声明最小直接依赖。
- 修正旧项目审计中被误改的 `zcarcloud/common` 原始路径。

### 设计决定 / Design Decisions

- `core/` 只是基础设施分类目录，不再代表“一次链接全部基础库”的 target。
- 只有出现被多个同级模块共享、且无法归属现有领域的接口或状态类型时，才考虑
  `modules/common`；禁止用它收纳暂时不知道放哪里的代码。

### 验证结果 / Verification

- 标准 `build/` 完成 38/38 构建步骤。
- `cockpit_smoke_test` 通过。
- 完整 `scripts/run_smoke.sh` 链路通过。

## 2026-06-20 - 核心、模块与驱动分层 / Core, Module, and Driver Layers

### 变更内容 / Changed

- 将通用基础设施从 `common` 迁移到 `core`。
- 将 `vehicle` 和平台无关的 `CanFrame` 迁移到 `modules`。
- 将 Linux `SocketCan` 迁移到 `drivers/socketcan`，并新增独立 `socketcan` target。
- protobuf 契约迁移到顶层 `proto`，生成代码继续放在 `build` 目录。
- 顶层 CMake 仅聚合 `core`、`modules`、`drivers` 和产品目录。

### 设计决定 / Design Decisions

- `core` 只提供配置、日志、生命周期和基础工具，不包含领域模型或硬件访问。
- `modules` 保存平台无关的领域能力；`drivers` 保存 Linux 与硬件适配。
- `socketcan` 依赖 `can`，反向依赖禁止；服务和工具按需链接最小 target。
- 内核模块和设备树可放在对应 `drivers/<device>` 下，但不进入默认用户态构建。

### 验证结果 / Verification

- `core`、`modules`、`drivers` 分层在标准 `build/` 中配置和构建成功。
- 独立生成 `libcan.a` 和 `libsocketcan.a`，依赖传播正确。
- `cockpit_smoke_test` 与完整 smoke 链路通过。

## 2026-06-20 - common 构建模块化 / Common Build Modularization

> 本节记录迁移前的首次模块化；当前目录边界以上一节为准。

### 变更内容 / Changed

- 参考 `znavigator`，当时为配置、日志、生命周期、工具、车辆与 CAN 模块分别增加
  独立 `CMakeLists.txt`。
- 当时由统一聚合入口管理各子目录；该入口现已拆成 `core`、`modules` 和 `drivers`。
- 每个模块声明自己的直接依赖。
- `core` 改为 INTERFACE 兼容聚合 target，现有服务暂时无需整体改链接方式。

### 设计决定 / Design Decisions

- 新代码优先链接实际需要的最小 target。
- 不提前创建没有源码的 `audio`、`ai` 空 target。
- `core` 不聚合 `can`、`audio` 或 `ai` 等功能模块。

### 验证结果 / Verification

- 标准 `build/` 目录重新配置并完成 37/37 构建步骤。
- 成功生成 `libconfig.a`、`liblogging.a`、`libruntime.a`、`libutils.a`、
  `libvehicle.a` 和 `libcan.a`。
- `cockpit_smoke_test` 通过。
- 完整 `scripts/run_smoke.sh` 链路通过。

## 2026-06-20 - 文档收拢 / Documentation Consolidation

### 变更内容 / Changed

- 根 `README.md` 更新为当前仓库、已实现能力和可运行命令入口。
- `docs/README.md` 调整为文档导航和文档优先级说明。
- 纳入完整架构蓝图 `docs/architecture_refined_v0.3.md`。
- 架构蓝图中的项目名、安装路径和当前阶段说明统一为 cockpit-system。
- 项目名、仓库名和部署目录统一使用 `cockpit-system`。
- 明确当前只维护 `cockpit-system` 主仓库，云端前后端是未来可选拆分。
- 修正当前架构快照的蓝图链接，并统一 scope/modularization 中的仓库名和 CAN 状态。

### 验证结果 / Verification

- 文档链接和旧项目名通过文本检查。
- 清理旧 CMake cache 后，使用标准 `bash scripts/build.sh` 重新配置并构建成功。
- `cockpit_smoke_test` 通过。

## 2026-06-20 - SocketCAN 基础层 / SocketCAN Foundation

### 变更内容 / Changed

- 新增内部 CMake target：`can`。
- 新增 `CanFrame`，支持标准帧/扩展帧 ID、DLC、标志位校验和 candump 格式输出。
- 新增 `SocketCan`，支持 fd RAII、移动语义、接口绑定、发送、poll 超时和接收。
- `can-simulator` 新增 `--backend stdout|socketcan`。
- 当时新增 `can.simulator_backend` 配置（现为 `hardware.can.simulator_backend`），安全默认值为 `stdout`。
- `cockpit_smoke_test` 增加 CAN 帧测试。
- 参考并审计 `/home/ffz/code/project/无人车/can/can_ws/src/can_analyze`。
- 变更记录调整为中英双语格式。

### 设计决定 / Design Decisions

- Linux/SocketCAN 细节封装在 `SocketCan` 内部。
- 不复制 ROS2 依赖、全局 socket、分离线程和固定全局帧缓冲区。
- 没有 `vcan0` 或真实 CAN 硬件时，`stdout` 模式仍必须可运行。

### 验证结果 / Verification

- 使用 GCC 11.4.0 和 Ninja 在标准 `build/` 目录中构建成功。
- `cockpit_smoke_test` 通过。
- 默认 `stdout` CAN 后端下，完整 `scripts/run_smoke.sh` 链路通过。
- 本节记录时尚未配置 `vcan0`；该验证已于 2026-06-21 完成，见最新记录。
