# Smart Cockpit System

运行在 Jetson/Linux 上的模块化智能座舱系统。

当前重点是单机车端链路：车辆状态、音频、语音、摄像头、Qt/QML UI、诊断工具和部署。云端
平台、Web 前端和量产级安全能力暂不属于当前阶段。

## 当前能力

- SocketCAN/mock 车辆数据和 VehicleState gRPC streaming。
- ROS 风格 `topic list/info/echo/hz` 调试。
- ALSA 录音/播放、AudioFrame、SPSC ring、PCM 进程间传输、mock VAD 和语音分段。
- mock ASR/TTS 语音链路，以及显式 Agent 构建中的 Sherpa KWS / Silero VAD / SenseVoice / Kokoro
  provider；Ubuntu x86_64 已有固定资源 smoke，默认构建仍不加载模型。
- 语音意图、动作分发、车辆状态查询和 Qt 相机页面控制。
- 三条固定 Camera pipeline：`argus_isp`、USB `uvc` 和 `software_isp`；设备、输入 fourcc 与 ISP 组合严格校验，另保留独立 V4L2 kernel driver 学习线。
- `software_isp` 已支持 IMX219 RG10 MMAP、`bypass_mode=0`、采集/ISP 双线程有界队列、BLC/WB/demosaic/gamma 和 ARM64 NEON；公开输出为 BGRx，1920×1080@30 实测 0 丢帧。
- Jetson Argus CSI 通过 GStreamer 获取 ISP 后 BGRx；CUDA ISP 目前仅为性能 prototype，完成了性能和图像质量对比，尚未替换 CPU 默认路径。
- 相机帧 POSIX shared memory 双缓冲。
- 基于最新共享帧的 JPEG 拍照，支持 camera-ctl 和 Qt UI。
- 研发录包会话、事件与文件索引、artifact 校验、时间线、报告和异常中断恢复。
- Qt 6/QML 车辆和相机界面。
- HMI 动态模块监管 Qt UI 生命周期和崩溃恢复。
- 受控 App Launcher 状态模型只接受三个固定应用 ID；默认后端不启动任何第三方进程，路径、参数和
  shell 不进入接口。
- 本地媒体控制合同提供 play/pause/resume/next/stop 状态机和固定 track ID；生产 backend 默认不可用，
  现有提示音/TTS PCM RPC 不冒充长音乐播放器。
- 独立 Navigator `media` module 提供 `MediaControl` gRPC、disabled/mock/受控 GStreamer provider
  和健康状态；仅在 `ui` mode 启动，默认 disabled，不增加 systemd service。
- `cockpit-ctl` 聚合状态和各类 probe/ctl 工具。
- Navigator 统一入口、动态业务模块、运行模式切换、有界本地 IPC 和故障重启限制。
- STM32/SR501 哨兵 fake 基线：Vehicle Driver 发布 typed `ChassisEvent`，独立 Sentinel module 完成
  布防、重复/冷却抑制、相机抓拍、Recording 事件和 HMI 状态；真实 CAN ID/位布局待联调固定。
- 独立 `bridge` module 提供 typed goal/cancel/pose/status gRPC、deterministic fake provider 和可选
  `Ros2Nav2Provider`；Ubuntu 已通过官方 Nav2 map/planner/controller/BT Navigator minimal smoke，
  production 仍默认 disabled，测试 `/cmd_vel` 不连接硬件或 CAN。
- Navigator 周期状态/健康采样、受控故障注入、JSON 稳定性报告和失败自动诊断快照。
- systemd、Release 打包，以及 `safe-ota` 校验、安装、健康检查和失败回滚原型。
- llama.cpp b10456 托管的 Qwen3.5-2B production 与 4B comparison smoke；Qwen3.5 语音请求关闭
  thinking，LLM 只能生成用户可见正文，不能执行车辆动作。

## 架构

```text
systemd → cockpit/navigator → module child → cockpit/library / agent

Linux / Hardware → drivers → modules → library / agent / apps / tools
drivers 之上的项目层共用 core；跨进程契约位于 proto
```

- `cockpit/core`：配置、日志、Runtime、事件和 IPC。
- `cockpit/drivers`：ALSA、SocketCAN、V4L2。
- `cockpit/modules`：audio、camera、vehicle、voice 等领域能力。
- `cockpit/navigator`：统一入口、模式、模块进程和状态管理。
- `cockpit/library`：进程级动态业务模块和资源所有权。
- `agent`：语音、会话、动作和本地 AI 应用层。
- `cockpit/apps`：Qt/QML UI。
- `tools`：模拟器和诊断工具。

```text
cockpit-system/
├── _output/   WSL 构建、打包和运行输出（不入库）
├── cockpit/   产品源码
├── tools/     诊断与模拟器
├── tests/     C++ 测试源码与测试夹具
├── configs/   开发与生产运行配置
├── cmake/     构建模块
├── deploy/    安装、回滚、systemd 和发布声明
├── scripts/   构建、打包、环境准备、本地运行和测试脚本
├── ros2/      cockpit 自有 ament packages；官方 ROS2/Nav2 仍位于 /opt/ros/humble
└── docs/      文档
```

详细说明见：

- [文档导航](docs/文档导航.md)
- [当前实现状态](docs/实现状态.md)
- [系统架构](docs/系统架构.md)
- [项目路线图](docs/项目进度总览.md)
- [语音 Agent 阶段任务](docs/语音Agent阶段任务.md)
- [ROS 2 与 Nav2 使用说明](docs/ROS2与Nav2使用说明.md)

## 环境准备

WSL2/Ubuntu 22.04：

```bash
bash scripts/setup/dependencies.sh
```

## 构建和测试

```bash
bash scripts/build.sh                         # GCC Debug 开发构建和 CTest
bash scripts/build.sh --type release          # GCC Release 正式 Linux 构建
bash scripts/tests/smoke.sh
bash scripts/ros2/configure.sh
bash scripts/ros2/build.sh
bash scripts/ros2/test.sh
bash scripts/tests/sherpa-voice-smoke.sh
bash scripts/tests/navigator-stability.sh --duration 300 --interval 5 --fault crash --fault-count 3
bash scripts/ai/prepare-sherpa-runtime.sh
bash scripts/ai/prepare-voice-models.sh
# 设置已审查的 commit、源码来源及两个 SHA-256 后执行：
# COCKPIT_LLAMA_CPP_REVISION / COCKPIT_LLAMA_CPP_SOURCE_SHA256
# COCKPIT_LLM_MODEL_FILE（或 URL）/ COCKPIT_LLM_MODEL_SHA256
# 默认准备 Qwen3.5-2B；4B 对照需设置 COCKPIT_LLM_MODEL_PROFILE=comparison
bash scripts/ai/prepare-llama-runtime.sh
bash scripts/ai/prepare-llm-model.sh
bash scripts/ai/build-llama-runtime.sh       # native runtime build + manifest/ldd/model checks
bash scripts/tests/llama-server-smoke.sh
# 一次准备 runtime、2B/4B 模型并运行 smoke（禁止 sudo）：
bash scripts/ai/bootstrap-llm.sh
# Hugging Face 直连不可用时：
COCKPIT_HF_ENDPOINT=https://hf-mirror.com bash scripts/ai/bootstrap-llm.sh
```

本地 LLM 基线固定为 llama.cpp `b10456`（`f275595dd16f7ed3d644d4d8159b14b305960479`）。
production 使用 `unsloth/Qwen3.5-2B-GGUF` 的 `Qwen3.5-2B-Q4_K_M.gguf`，comparison 使用
`unsloth/Qwen3.5-4B-GGUF` 的 `Qwen3.5-4B-Q4_K_M.gguf`；精确模型 revision 和 SHA-256 见
[部署说明](docs/部署说明.md)。`scripts/ai/bootstrap-llm.sh` 必须以普通用户运行，已有校验正确的下载缓存会直接复用。

`build.sh` 统一使用 GCC：Debug 用于开发、CTest 和 smoke，Release 用于正式构建和发布包。

构建目录：

```text
_output/build/x86_64-debug/
_output/build/x86_64-release/
_output/build/x86_64-sherpa-debug/
_output/build/x86_64-asan/
_output/build/x86_64-tsan/
```

WSL 生成物统一放在 `_output/{build,install,runtime}`。可通过 `COCKPIT_OUTPUT_DIR` 修改整个输出根目录；
运行脚本会自动把日志、数据、报告和临时测试音频写入 `_output/runtime` 的对应子目录。
不要在仓库根目录运行 `cmake -B build`；CMake 会拒绝该目录。VS Code CMake Tools 已固定使用
`_output/build/vscode-${buildType}`。历史根目录 `build/` 和 `logs/` 已分别迁入
`_output/build/legacy/`、`_output/runtime/logs/legacy/`。

查看并归档历史构建和 smoke/stability 运行目录：

```bash
bash scripts/cleanup-output.sh
bash scripts/cleanup-output.sh --apply
```

脚本默认只预览，不删除文件；`--apply` 只把明确的历史目录移动到 `_output/build/archive/` 或
`_output/runtime/reports/archive/`，不会处理 `_output/ai` 模型、下载缓存或临时资源。

## 常用工具

```bash
export COCKPIT_RUNTIME_DIR="$PWD/_output/runtime"
_output/build/x86_64-debug/bin/topic list --config configs/development.yaml
_output/build/x86_64-debug/bin/audio-probe --list --config configs/development.yaml
_output/build/x86_64-debug/bin/camera-probe --list --config configs/development.yaml
_output/build/x86_64-debug/bin/recording-ctl --start --trigger manual \
  --config configs/development.yaml
_output/build/x86_64-debug/bin/cockpit-ctl status --config configs/development.yaml
_output/build/x86_64-debug/bin/cockpit-ctl health --config configs/development.yaml
_output/build/x86_64-debug/bin/cockpit-ctl runtime status --socket /tmp/cockpit-navigator.sock
```

启动统一运行时：

```bash
_output/build/x86_64-debug/bin/cockpit-navigator \
  --config configs/development.yaml \
  --module-dir _output/build/x86_64-debug/lib/cockpit/modules
```

运行 Qt UI：

```bash
bash scripts/run/cockpit-ui.sh
bash scripts/run/cockpit-ui.sh --camera
```

Jetson CSI 默认使用 `argus_isp + nvargus://0`。USB UVC 摄像头必须显式选择输入
fourcc 策略；1080p30 常用 MJPEG，设备只提供未压缩格式时选择 YUYV：

```bash
CAMERA_PIPELINE=uvc CAMERA_UVC_INPUT_FORMAT=mjpeg CAMERA_DEVICE=/dev/video0 \
  bash scripts/run/cockpit-ui.sh --camera
```

## Camera V4L2/Argus/ISP 性能基线

```bash
# Argus + Jetson 硬件 ISP
_output/build/arm64-debug/bin/camera-preview-probe \
  --backend argus --device nvargus://0 --width 1920 --height 1080 \
  --fps 30 --frames 30 --config configs/development.yaml

# V4L2 MMAP + Software ISP（需要 Jetson tegra-video 的 bypass_mode=0）
_output/build/arm64-debug/bin/camera-preview-probe \
  --backend software_isp --device /dev/video0 --width 1920 --height 1080 \
  --fps 30 --frames 300 --timeout-ms 30000 --config configs/development.yaml

# USB UVC MJPEG；若设备枚举只支持 YUYV，改成 --uvc-input-format yuyv
_output/build/arm64-debug/bin/camera-preview-probe \
  --backend uvc --uvc-input-format mjpeg --device /dev/video1 \
  --width 1920 --height 1080 --fps 30 --frames 300 \
  --timeout-ms 30000 --config configs/development.yaml
```

Jetson 实测 Direct V4L2 + NEON Software ISP 在 1920×1080 下约 30 fps，ISP 平均约 16.3 ms，最近
2048 帧 dequeue/queue-to-output P50 约 16.6 ms，队列丢帧和 source sequence gap 均为 0；进程 CPU
约 111--117%。CUDA prototype
的 H2D/kernel/D2H 约 7.0 ms，进程 CPU 约 39%，但和 CPU OpenCV demosaic 的红蓝通道尚未完全对齐，
暂不作为默认生产 backend。

`CameraFrame.timestamp_ms` 保留为主机接收时刻的 Unix epoch 毫秒兼容字段；源时间使用
`source_timestamp_ns + source_clock + source_timestamp_valid`，主机接收时间使用 `received_at_ns`。
V4L2 保留驱动 buffer 时间及 clock flag，GStreamer 使用 pipeline running-time PTS，跨 Camera/LiDAR
同步前必须按 clock domain 转换，不能直接混用。

底层 RAW 和 CPU/CUDA 对比入口：

```bash
_output/build/arm64-debug/bin/v4l2-mmap-capture --frames 10
_output/build/arm64-debug/bin/camera-isp-cpu-benchmark RAW 1920 1080 3840 300 cpu-bgrx.raw
_output/build/arm64-debug/bin/camera-isp-cuda-benchmark RAW 1920 1080 3840 300 cuda-bgrx.raw
```

## USB 摄像头权限

```bash
sudo usermod -aG video "$USER"
newgrp video
```

随后可执行：

```bash
_output/build/x86_64-debug/bin/camera-preview-probe \
  --backend uvc --uvc-input-format mjpeg --device /dev/video0 \
  --frames 30 --config configs/development.yaml
```

## ASR

当前仓库包含 Agent 内的 mock VAD/ASR/TTS 和完整 PCM 流水线。Sherpa KWS / Silero VAD /
SenseVoice provider 代码作为显式 Agent 产品构建的一部分交付，基础构建不下载、编译或链接其内部
推理运行时。Ubuntu apt 只提供操作系统和平台依赖；`_output/ai` 统一放本地 runtime/model 资源。
Ubuntu x86_64 下的真实 Sherpa KWS/VAD/ASR/TTS smoke 入口是 `bash scripts/tests/sherpa-voice-smoke.sh`。
该入口还会启动真实 Navigator UI mode，并使用已有 WAV fixture 通过 Audio Driver 的
`wav:/绝对路径` capture source 重放 `你好小山` 和命令，验证进程间音频传输、VoiceInputGate、ASR、
安全 action 和响应播放；可单独运行 `bash scripts/tests/sherpa-service-voice-smoke.sh`。该 replay
不需要重新录音，且不替代真实 ALSA 环境下的声学验收。
SenseVoice 固定录音性能与安全路由基线可运行
`bash scripts/tests/sensevoice-asr-benchmark.sh`。该入口复用已有 Sherpa runtime、模型和 8 条真实录音，
不下载资源、不进入默认 CI；默认重复 3 轮，可通过 `COCKPIT_SENSEVOICE_BENCHMARK_REPETITIONS=10`
运行 80 次识别。CER 和整句准确率只统计两条有可靠人工参考文本的中英文 `open camera` fixture，
其余录音用于重复输出、延迟、RSS 和 fail-closed 路由验证，不能冒充完整车控语料准确率。
Kokoro TTS 模型使用 `bash scripts/ai/prepare-kokoro-tts.sh` 准备；该脚本只安装到 `_output/ai`，
不使用 sudo。默认构建仍不下载或加载 Sherpa/TTS 模型。
同一 Kokoro provider 实例的重复合成和 RSS 基线可运行
`bash scripts/tests/kokoro-tts-stability.sh`；默认 16 轮，可通过
`COCKPIT_KOKORO_STABILITY_ITERATIONS=32` 增加到 32 轮。该入口复用已有 runtime/model，不下载资源，
也不进入默认 CI。
固定中文、英文、中英混读、数字单位、标点和长回复的延迟/RTF 基线使用
`bash scripts/tests/kokoro-tts-benchmark.sh`。它还比较整段回复和首个可播放分段，验证空文本、过期
deadline、异常符号、真实取消以及取消后的 provider 恢复；默认每类 3 轮，可用
`COCKPIT_KOKORO_BENCHMARK_REPETITIONS` 调整为 1-10 轮。
真实服务级重复播放可设置 `COCKPIT_SERVICE_VOICE_REPETITIONS=8` 后运行
`bash scripts/tests/sherpa-service-voice-smoke.sh`。首轮经过唤醒，后续轮次复用 follow-up，逐轮要求
ASR、确定性 action、Kokoro、Audio Driver playback receipt 一一完成，并检查播放错误、xrun、设备错误
和服务进程树 RSS；该模式不下载资源，也不替代并发队列或现场回声测试。
本地 LLM 默认关闭；生产候选为 Qwen3.5-2B Q4_K_M，Qwen3.5-4B Q4_K_M 仅作资源/质量对照。
显式准备 llama.cpp runtime 和对应 GGUF 后，使用
`bash scripts/tests/llama-server-smoke.sh` 验证真实 server 与项目 client 的接口。
产品启用时由 Agent 托管 `llama-server` 子进程及其 health/restart/cleanup，不增加独立 systemd service。
client 增量消费 SSE token stream，分别执行首 token 超时和整次回复总超时；取消会关闭当前本地连接。
Qwen3.5 的语音请求显式设置 `chat_template_kwargs.enable_thinking=false`，只把 `delta.content` 作为
用户可见或可播报正文；`reasoning_content` 不会进入回复。
Qwen3.5 的原生有界质量、并发、RSS、温度和 tokens/s 基线使用
`bash scripts/tests/llama-server-benchmark.sh`，结果写入当前架构 Debug 构建目录的 `llm-benchmark`。
Jetson 默认只验证 production 2B 并显式要求 GPU offload；x86_64 默认验证 2B/4B。该基准只使用本地已校验资源、强制 offline 并只监听 loopback；质量输出保留供人工审核，不将小样本自动评分当作生产质量结论。
Sherpa runtime 独立交付必须通过 `bash scripts/ai/prepare-sherpa-runtime.sh`，提供固定
`COCKPIT_SHERPA_RUNTIME_SHA256` 的归档；归档必须含 `include/`、`lib/` 和 `LICENSE`，脚本验证私有
`ldd`、许可证哈希并以临时目录和原子 rename 安装，不使用 sudo。Stage 14 资源切换使用
`scripts/ai/manage-resource.sh stage|activate|rollback|status RESOURCE_ROOT [RELEASE]`；stage 不会自动激活生产模型。
显式 Sherpa Agent 构建的 Navigator modules 在编译阶段启用 hidden visibility，链接阶段只导出
`CockpitModuleGetApi`；`--exclude-libs,ALL`、version script 和安装态 `$ORIGIN/../..` RPATH 继续限制
模块符号和相对依赖。Sherpa/ONNX Runtime 现在作为独立、固定版本的 `_output/ai` 交付物验证，
不安装到系统目录，也不进入默认构建。

## 提交规范

```text
[feature]: add ...
[fix]: handle ...
[refactor]: organize ...
[docs]: update ...
```

每批代码变更同步记录到 [变更记录](docs/变更记录.md)。
