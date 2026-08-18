# Smart Cockpit System

运行在 Jetson/Linux 上的模块化智能座舱系统。

当前重点是单机车端链路：车辆状态、音频、语音、摄像头、Qt/QML UI、诊断工具和部署。云端
平台、Web 前端和量产级安全能力暂不属于当前阶段。

## 当前能力

- SocketCAN/mock 车辆数据和 VehicleState gRPC streaming。
- ROS 风格 `topic list/info/echo/hz` 调试。
- ALSA 录音/播放、AudioFrame、SPSC ring、PCM 进程间传输、mock VAD 和语音分段。
- mock ASR/TTS 语音链路，以及可选的 Sherpa KWS / Silero VAD / SenseVoice provider 代码骨架。
- 语音意图、动作分发、车辆状态查询和 Qt 相机页面控制。
- V4L2/GStreamer USB 摄像头和 Jetson Argus CSI 摄像头预览。
- 相机帧 POSIX shared memory 双缓冲。
- 基于最新共享帧的 JPEG 拍照，支持 camera-ctl 和 Qt UI。
- 研发录包会话、事件与文件索引、artifact 校验、时间线、报告和异常中断恢复。
- Qt 6/QML 车辆和相机界面。
- HMI 动态模块监管 Qt UI 生命周期和崩溃恢复。
- `cockpit-ctl` 聚合状态和各类 probe/ctl 工具。
- Navigator 统一入口、动态业务模块、运行模式切换、有界本地 IPC 和故障重启限制。
- Navigator 周期状态/健康采样、受控故障注入、JSON 稳定性报告和失败自动诊断快照。
- systemd、Release 打包，以及 `safe-ota` 校验、安装、健康检查和失败回滚原型。

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
└── docs/      文档
```

详细说明见：

- [文档导航](docs/文档导航.md)
- [当前实现状态](docs/实现状态.md)
- [系统架构](docs/系统架构.md)
- [项目路线图](docs/项目进度总览.md)
- [语音 Agent 阶段任务](docs/语音Agent阶段任务.md)

## 环境准备

WSL2/Ubuntu 22.04：

```bash
bash scripts/install-dependencies.sh
```

## 构建和测试

```bash
bash scripts/build.sh                         # GCC Debug 开发构建和 CTest
bash scripts/build.sh --type release          # GCC Release 正式 Linux 构建
bash scripts/tests/smoke.sh
bash scripts/tests/sherpa-voice-smoke.sh
bash scripts/tests/navigator-stability.sh --duration 300 --interval 5 --fault crash --fault-count 3
bash scripts/prepare-sherpa-runtime.sh
bash scripts/prepare-voice-models.sh
# 设置已审查的 commit、源码来源及两个 SHA-256 后执行：
# COCKPIT_LLAMA_CPP_REVISION / COCKPIT_LLAMA_CPP_SOURCE_SHA256
# COCKPIT_LLM_MODEL_FILE（或 URL）/ COCKPIT_LLM_MODEL_SHA256
# 默认准备 Qwen3.5-2B；4B 对照需设置 COCKPIT_LLM_MODEL_PROFILE=comparison
bash scripts/prepare-llama-runtime.sh
bash scripts/prepare-llm-model.sh
bash scripts/tests/llama-server-smoke.sh
# 一次准备 runtime、2B/4B 模型并运行 smoke（禁止 sudo）：
bash scripts/llm.sh
# Hugging Face 直连不可用时：
COCKPIT_HF_ENDPOINT=https://hf-mirror.com bash scripts/llm.sh
```

本地 LLM 基线固定为 llama.cpp `b10456`（`f275595dd16f7ed3d644d4d8159b14b305960479`）。
production 使用 `unsloth/Qwen3.5-2B-GGUF` 的 `Qwen3.5-2B-Q4_K_M.gguf`，comparison 使用
`unsloth/Qwen3.5-4B-GGUF` 的 `Qwen3.5-4B-Q4_K_M.gguf`；精确模型 revision 和 SHA-256 见
[部署说明](docs/部署说明.md)。`scripts/llm.sh` 必须以普通用户运行，已有校验正确的下载缓存会直接复用。

`build.sh` 统一使用 GCC：Debug 用于开发、CTest 和 smoke，Release 用于正式构建和发布包。

构建目录：

```text
_output/build/x86_64-debug/
_output/build/x86_64-release/
_output/build/arm64-debug/
_output/build/arm64-release/
```

WSL 生成物统一放在 `_output/{build,install,runtime}`。可通过 `COCKPIT_OUTPUT_DIR` 修改整个输出根目录；
运行脚本会自动把日志、数据和报告写入 `_output/runtime`。

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
bash scripts/run-cockpit-ui.sh
bash scripts/run-camera-ui.sh
```

Jetson CSI 默认使用 `nvargus://0`。USB 摄像头可显式指定：

```bash
CAMERA_DEVICE=/dev/video0 bash scripts/run-camera-ui.sh
```

## USB 摄像头权限

```bash
sudo usermod -aG video "$USER"
newgrp video
```

随后可执行：

```bash
_output/build/x86_64-debug/bin/camera-preview-probe \
  --device /dev/video0 --frames 30 --config configs/development.yaml
```

## ASR

当前仓库包含 Agent 内的 mock VAD/ASR/TTS 和完整 PCM 流水线。Sherpa KWS / Silero VAD /
SenseVoice provider 代码作为显式 Agent 产品构建的一部分交付，基础构建不下载、编译或链接其内部
推理运行时。Ubuntu apt 只提供操作系统和平台依赖；`_output/ai` 统一放本地 runtime/model 资源。
Ubuntu x86_64 下的真实 Sherpa KWS/VAD/ASR/TTS smoke 入口是 `bash scripts/tests/sherpa-voice-smoke.sh`。
Kokoro TTS 模型使用 `bash scripts/prepare-kokoro-tts.sh` 准备；该脚本只安装到 `_output/ai`，
不使用 sudo。默认构建仍不下载或加载 Sherpa/TTS 模型。
本地 LLM 默认关闭；生产候选为 Qwen3.5-2B Q4_K_M，Qwen3.5-4B Q4_K_M 仅作资源/质量对照。
显式准备 llama.cpp runtime 和对应 GGUF 后，使用
`bash scripts/tests/llama-server-smoke.sh` 验证真实 server 与项目 client 的接口。
产品启用时由 Agent 托管 `llama-server` 子进程及其 health/restart/cleanup，不增加独立 systemd service。
client 增量消费 SSE token stream，分别执行首 token 超时和整次回复总超时；取消会关闭当前本地连接。
Qwen3.5 的语音请求显式设置 `chat_template_kwargs.enable_thinking=false`，只把 `delta.content` 作为
用户可见或可播报正文；`reasoning_content` 不会进入回复。

## 提交规范

```text
[feature]: add ...
[fix]: handle ...
[refactor]: organize ...
[docs]: update ...
```

每批代码变更同步记录到 [变更记录](docs/变更记录.md)。
