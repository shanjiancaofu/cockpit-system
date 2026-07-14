# 当前架构概览

本文只描述当前代码已经形成的真实运行架构。模块完成度见 [实现状态.md](实现状态.md)，近期推进
顺序见 [项目进度总览.md](项目进度总览.md)。

## 项目定位

`cockpit-system` 是运行在 Jetson/Linux 上的智能座舱车端系统。当前保持单仓库，通过 CMake
target 和职责目录实现内部模块化，不提前拆分云端前端、后端或共享协议仓库。

项目采用 C++17、CMake、Ninja、protobuf 和 gRPC。动态运行时参考 `znavigator` 的配置、连接、
动态加载和父子进程组织，但使用座舱领域命名，不照搬内部代号、全局业务对象和历史兼容结构。

## 分层结构

```text
systemd → cockpit-navigator                 唯一长运行入口
              ↓ fork + exec
         module child → cockpit/library    进程级动态业务模块
                              ↓
                       cockpit/modules     进程内领域实现
                              ↓
                       cockpit/drivers     Linux/硬件适配
                              ↓
                         cockpit/core      通用基础设施
```

主要目录：

```text
cockpit/
├── apps/cockpit-ui/       Qt 6/QML 车机界面
├── core/                  通用基础设施
├── drivers/               Linux/硬件适配
├── library/               可由 Navigator 加载的进程级业务模块
├── modules/               audio、camera、recording、vehicle、voice
├── navigator/             统一入口、配置、连接、加载和子进程管理
└── proto/                 protobuf/gRPC 契约
tools/                     诊断和模拟工具
tests/                     单元测试与 smoke test
```

## 进程职责

`cockpit-navigator` 是车端唯一长运行入口。进程级模块表和 mode 组合固定在
`navigator/run_config/run_config.cc`，不由业务 YAML 暴露动态库路径。父进程只把统一的
`config.yaml` 路径传给 module child；子进程通过版本化 C ABI 和 `dlopen` 加载一个 `library/*`
动态库。Navigator 负责模式切换、显式启停、状态查询、崩溃重启限制和退出回收，本地 Unix Socket
控制接口不依赖 transfer 模块。

当前已经迁入真实 Runtime 的进程级模块：

- `vehicle_driver`：独占 CAN 或 mock 车辆数据源，发布 `VehicleState`。
- `transfer`：聚合车辆状态，向 UI、topic 和语音动作提供数据。
- `audio_driver`：独占麦克风和扬声器，运行采集、VAD、分段、ASR 和 TTS 播放。
- `camera_driver`：独占摄像头，负责预览生命周期和共享内存写入。
- `agent`：订阅识别文本，执行模型、意图、动作和语音回复编排。
- `hmi`：启动并监管 `cockpit-ui`，保持 Qt 主线程事件循环独立，向 Navigator 汇报退出状态。
- `recording`：面向研发诊断，订阅车辆状态并管理持久化录包会话。
- `carupload`：保留原 MQTT 上传占位行为，尚不代表真实云端传输。
- `upgrader`：在独占的 upgrade mode 中校验并安装候选版本，发布持久化事务结果。

UI 构建中，`normal` 启动 transfer、三类 driver、agent 和 hmi；`development` 在此基础上增加 recording；`cloud`
启动 transfer、vehicle_driver 和 carupload；`upgrade` 只启动 upgrader，避免安装期间继续占用业务
资源。headless 构建不把 hmi 加入 mode；`debugger/calibration/watchdog` 只保留 ABI 骨架，不进入可运行
mode，手动启动会明确失败。

三个 systemd target 分别启动一个 `cockpit-navigator@<mode>.service` 实例，target 之间互斥。
旧独立 service 二进制不再构建或打包；详细 smoke、UI 联调和 vcan smoke 都通过 Navigator 选择模块
组合。`ProcessRuntime` 只服务于诊断工具等独立可执行程序，不再管理长运行的产品业务资源。

HMI module child 不在 ABI `poll()` 中模拟 Qt event loop，而是启动同目录的 `cockpit-ui`。Qt 仍在自己的
主线程执行 `QGuiApplication::exec()`；UI 退出会通过 HMI `Poll()` 触发 Navigator 的重启限制，HMI
module child 异常退出则由 Linux parent-death signal 终止 UI，避免留下脱管界面进程。

## 通信模型

```text
同线程             函数调用
同进程控制         callback / EventQueue
音频连续流         SPSC RingBuffer
相机跨进程帧       POSIX Shared Memory 双缓冲
控制、状态、文本   gRPC unary / streaming
外部云端           MQTT / WebSocket（后续）
```

gRPC 不承载 PCM、图片等高频大数据。控制面和数据面分离：gRPC 管理生命周期、状态和文本；
ring buffer 或共享内存传输连续数据。

## 车辆链路

```text
can-simulator / SocketCAN
    → vehicle_driver
    → VehicleState gRPC stream
    → transfer
    → cockpit-ui / topic / voice vehicle query
```

当前 CAN 映射是原型格式；正式车辆接入必须基于确认后的 DBC 或信号文档。

语音车辆状态查询通过 gateway gRPC 执行。打开相机和播放音乐属于 HMI 动作边界，当前
`LocalHmiCommandProvider` 只记录 handoff，尚未驱动 Qt 页面或媒体播放器，因此不计为已闭环动作。

## 音频与语音链路

```text
ALSA microphone
    → AudioCaptureStream
    → AudioFrame（20 ms）
    → SPSC RingBuffer
    → Energy VAD
    → SpeechSegmenter
    → mock ASR / whisper.cpp
    → agent
    → intent / action
    → mock TTS
    → ALSA speaker
```

PCM 和语音片段保持在 `audio_driver` module child 内，只有 transcript、控制和指标通过 gRPC。

## 相机链路

```text
USB Camera
    → V4L2
    → GStreamer appsink
    → camera_driver
    → POSIX Shared Memory 双缓冲
    → Qt camera worker
    → QML Camera 页面
```

UI 能区分等待首帧、实时画面、卡帧、最后一帧和共享内存断开，并在 writer 重启后自动重连。
共享帧槽使用 robust process-shared mutex；writer 异常退出后，新实例会回收遗留的 POSIX shared
memory，reader 会拒绝布局、stride 或 payload 长度不一致的帧。
拍照请求通过 gRPC 到 camera-service，服务读取共享内存最新帧并用 GStreamer 编码 JPEG；
camera-ctl 和 Qt UI 都不直接访问摄像头设备。

camera-service 的运行期看门狗检查 preview source 是否仍在运行以及最后收帧时间，将故障区分为
`source_disconnected`、`no_frames` 和 `frame_stalled`。WSL 可将 `capture_backend` 切换为
`synthetic`，通过同一个 `CameraPreviewSource` 边界注入无帧、卡帧和断流；恢复仍使用正式
start/recover 状态机，因此合成测试与真实 GStreamer pipeline 共享指标和控制逻辑。

## 服务健康语义

各长运行服务通过 `ServiceHealth` 暴露统一状态，`cockpit-ctl` 和 Qt Dashboard 复用
`core/health` 的名称、严重度和健康检查规则：

- `OK`：服务及其核心能力正常。
- `DISABLED`：能力被配置或控制命令主动关闭，服务本身正常，不视为故障。
- `DEGRADED`：服务仍可用，但核心输入、依赖或能力部分受损。
- `UNKNOWN`：尚未取得可信状态，包括 gRPC 不可达；协议中的 `UNSPECIFIED` 也按此状态解释。
- `FAULTED`：服务已确认进入故障状态，需要恢复或人工处理。

严重度顺序为 `OK < DISABLED < DEGRADED < UNKNOWN < FAULTED`。脚本化 health check 接受
`OK`、`DISABLED` 和 `DEGRADED`，对 `UNKNOWN`、`FAULTED` 返回失败；状态页仍逐项展示全部状态，
避免把主动关闭、能力下降和不可达混为一类。

cockpit-ui 在进程内保留最近 32 条状态切换，初次采样只建立基线，不生成虚假事件。每个服务记录
最近一次 degraded/faulted 的状态、时间和原因，恢复为 OK 后仍可在 Dashboard 和 Diagnostics 页面
追溯。历史不写数据库，UI 重启后清空；长期运行证据由 `run_navigator_stability.sh` 输出到构建目录。

## 诊断 CLI 输出

控制面诊断工具共用 `tools/diagnostics`，支持 `--output text|json`。JSON 状态直接由 protobuf 官方
转换生成，保留 proto 字段名；订阅类命令使用 JSON Lines。统一退出码为：0 成功、1 操作或 RPC
失败、2 参数错误、3 健康状态不通过。`cockpit-ctl status` 在部分服务离线时仍输出完整聚合文档，
`cockpit-ctl health` 则通过退出码 3 明确通知部署脚本。

ALSA 设备枚举、WAV 录放等本地硬件操作继续输出文本，它们不是稳定的控制面数据合同。

## 研发录包链路

```text
vehicle-data-service
    → VehicleState gRPC stream
    → recording-service
    → sessions/.recording_<id>/vehicle_state.jsonl
camera/voice/audio metadata
    → asynchronous recording publisher
    → recording-service event/data-file writer
    → sessions/.recording_<id>/events.jsonl + data_files.jsonl + artifacts/
    → sessions/<id>/manifest.json + COMPLETE
```

`recording-ctl` 通过 gRPC 启动、停止、查询、删除和清理会话。原始数据以文件为权威来源；
进程异常退出后，下次启动将未完成目录标记为 `interrupted_*`。目录索引从 manifest 重建，
并按最大会话数和总字节数清理最旧数据。`events.jsonl` 只保存轻量研发事件元数据，大块图片、
音频和视频仍以独立文件保存；需要纳入会话保留策略的文件会复制到 `artifacts/`，再写入相对路径
索引。camera 和 voice 使用有界后台队列投递录包数据，录包服务不可用不会阻塞用户主流程。该服务
属于研发诊断边界，不接收用户语音动作。

`recording-ctl --verify <session-id>` 通过 gRPC 执行会话完整性诊断，检查 `data_files.jsonl`
格式、会话内路径边界、文件存在性、普通文件类型、大小和受支持的 checksum。复制到会话的
artifact 会自动生成 `fnv1a64`，因此可检测内容变化；空 checksum 和暂不支持的算法会明确计入
unavailable，但不会误报为内容损坏。诊断会汇总全部 issue，CLI 在发现完整性问题时返回退出码 2。

`recording-ctl --verify-all` 按会话开始时间批量执行同一校验，返回 healthy、damaged、unavailable
计数和每个会话的轻量摘要。单个损坏 manifest 被隔离为 unavailable，不中断其他会话；详细 issue
仍通过单会话接口查询，避免长稳测试后一次 RPC 返回无界诊断数据。批量查询支持时间范围和 limit，
原始 JSONL 与 manifest 仍是权威来源，不为汇总结果额外引入数据库。

### 录包时间语义

recording-service 已提供多源时间线查询：读取 `vehicle_state.jsonl`、`events.jsonl` 和
`data_files.jsonl`，按主机侧 `timestamp_ms` 稳定排序，并支持时间范围和条数限制。损坏的 JSONL
行会被跳过并计数，单行损坏不会阻断整段研发复盘。它不负责同步或校准 ECU、相机、音频设备等
独立时钟，也不估算时钟偏移和漂移。

当前单机原型默认各服务使用同一 Jetson/WSL 主机时钟，时间线条目保留 `timestamp_ms`、`source`、
`kind`、label、原始 JSON 和可选 path。真实硬件提供 ECU timestamp、camera PTS 或 audio sample clock
后，再按需要增加 `source_timestamp`、`host_timestamp`、`monotonic_timestamp`、`clock_domain`、
`offset` 和 `uncertainty`，单独实现时间戳归一化；当前不提前引入 PTP 或漂移估计。

## 当前边界

已具备可运行的 WSL/Jetson 车机原型架构，但尚缺正式 DBC、真实 TTS、麦克风/扬声器标定、
Jetson CUDA/TensorRT 验证、音视频多源录包、MQTT、WebSocket、视觉 AI 和完整 LLM
应用层。

## 部署与可靠性边界

systemd 只负责 Navigator 的启动和重启，每个硬件资源只有一个 process owner；UI 崩溃只重启 HMI，
不关闭 driver module，重启后的 client 应重新连接，设备权限由部署配置固定。carupload 当前仍是可选占位。

升级控制面由一次性 `safe-ota` 发起：确认候选版本后切换到 `upgrade`，upgrader 校验 SHA256、安装到
独立版本目录并原子切换 `current`；切回原业务 mode 后执行健康检查，失败则恢复旧 symlink 并让
Navigator 重新加载旧模块。当前闭环用于 WSL 原型验证，不等同于量产 OTA。

当前已经实现 RAII、有界队列和丢弃指标、gRPC deadline/cancellation、signal stop、配置校验、
mock/null backend，以及 shared memory 的 name/layout/version/capacity 校验。尚未达到量产要求的部分
包括认证加密、secure boot、ASIL、硬件 watchdog、权限最小化、量产 OTA 和隐私授权。

## AI 安全边界

语音链路保持 `Audio -> VAD -> ASR provider -> Assistant -> typed ActionDispatcher -> TTS`。
车辆动作必须经过 allowlist 和类型校验；LLM 文本不能直接生成 CAN frame 或 shell command；网络失败
需要明确的本地 fallback；录音、文本和云端请求必须有隐私策略。mock provider 只用于链路验证。

## 新代码检查

1. 代码属于 core、module、driver、process、app 还是 tool？
2. 是否真的需要新进程、目录、target 或抽象？
3. 数据属于控制消息、小消息还是连续大块数据？
4. 是否复用现有接口，并保持 UI 不直接访问硬件？
5. 是否为外部边界提供 mock/null/fake 和失败路径？
6. 是否避免把 PCM、图像和点云放进 gRPC？
7. 是否增加与风险相称的测试并更新对应职责文档？
8. 是否通过 build、CTest、pre-commit 和相关 smoke？
