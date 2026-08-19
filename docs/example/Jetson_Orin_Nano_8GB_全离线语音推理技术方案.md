# Jetson Orin Nano 8GB 全离线语音推理技术方案

> SenseVoiceSmall + Qwen2.5 + Piper + ROS 2

| 目标硬件 | Jetson Orin Nano 8GB                            |
|----------|-------------------------------------------------|
| 运行方式 | 全离线，本地录音、本地识别、本地推理、本地合成  |
| 软件基线 | JetPack 6.x / Ubuntu 22.04 / ROS 2 Humble       |
| 核心模型 | SenseVoiceSmall / Qwen2.5 1.5B（可选3B）/ Piper |
| 文档版本 | V1.0 · 2026年7月                                |

适用范围：离线语音助手、机器人语音控制、边缘端人机交互

## 文档说明

| **目的** | **将现有离线语音交互项目整理为可部署、可测试、可验收的 Jetson Orin Nano 8GB 本地推理方案。** |
|----------|----------------------------------------------------------------------------------------------|
| 范围     | 仅包含离线 ASR、离线 LLM、离线 TTS；不考虑在线接口、云端回退和多模态视觉模型。               |
| 读者     | 机器人开发人员、ROS 2 开发人员、嵌入式 AI 工程人员及课程项目实施人员。                       |
| 前置条件 | 已具备 Jetson Orin Nano 8GB、麦克风、扬声器、NVMe 或足够容量的存储设备。                     |
| 依据     | 现有《离线语音交互.md》中的 ROS 2 节点、配置参数、模型接口与启动流程。                       |

## 1. 方案结论

> **推荐基线：**Jetson Orin Nano 8GB 上采用 SenseVoiceSmall 进行离线语音识别，Qwen2.5 1.5B 4bit 进行本地语言推理，Piper medium 进行离线语音合成。各模块按 ASR → LLM → TTS 严格串行执行。

- 不上传音频，不依赖互联网，不使用在线 API。
- LLM 首选 Qwen2.5 1.5B；系统稳定后再测试 3B。
- ASR、LLM、TTS 不并行抢占内存和 GPU。
- 代码和 ROS 2 配置以实际使用的 /asr 话题为准。
- 第一阶段不部署 LLaVA、视频分析、视觉定位和自主 Agent。

## 2. 系统总体架构

### 2.1 数据处理链路

1. ① 麦克风采集：PyAudio，16 kHz，单声道，16 bit
2. ② 语音活动检测：WebRTC VAD，判断开始说话与结束静音
3. ③ 离线语音识别：SenseVoiceSmall：WAV → 中文文本
4. ④ ROS 2 消息传输：asr.py 发布 /asr，model_service.py 订阅 /asr
5. ⑤ 本地语言模型：Ollama 调用 Qwen2.5 1.5B/3B 生成回答
6. ⑥ 离线语音合成：Piper：回答文本 → WAV
7. ⑦ 扬声器播放：播放完成后重新进入监听状态

### 2.2 ROS 2 节点职责

| **模块**     | **文件**                                  | **输入**          | **输出/职责**                           |
|--------------|-------------------------------------------|-------------------|-----------------------------------------|
| ASR 节点     | largemodel/largemodel/asr.py              | 麦克风音频        | VAD录音、SenseVoiceSmall识别、发布 /asr |
| 模型服务节点 | largemodel/largemodel/model_service.py    | /asr 文本         | 调用LLM、解析回复、调用TTS并播放        |
| 模型接口层   | largemodel/utils/large_model_interface.py | 音频/文本         | 封装SenseVoice、Ollama和Piper调用       |
| 配置层       | config/\*.yaml                            | ROS参数和模型路径 | 控制离线模式、模型名称、音频参数        |

> **话题名称：**原理说明中可能出现 /asr_text 和 /tts_text，但现有核心代码实际发布与订阅的是 /asr；完整流程中 model_service 直接调用 Piper 合成并播放，不必额外依赖 /tts_text。

## 3. 硬件与软件基线

### 3.1 硬件要求

| **项目**   | **最低要求**          | **建议配置**                      |
|------------|-----------------------|-----------------------------------|
| 计算平台   | Jetson Orin Nano 8GB  | 启用最高合适功耗模式并保持散热    |
| 存储       | 64GB 可用空间         | 128GB 以上 NVMe；模型和日志放NVMe |
| 音频输入   | USB麦克风或阵列麦克风 | 固定设备索引或建立稳定别名        |
| 音频输出   | USB/HDMI/3.5mm扬声器  | 能通过 aplay 正常播放             |
| 供电与散热 | 稳定供电              | 主动风扇，避免长时间推理降频      |

### 3.2 软件建议

- JetPack 6.x，Ubuntu 22.04。
- ROS 2 Humble，Python 3.10。
- CUDA、cuDNN、TensorRT 使用 JetPack 自带版本。
- Ollama 作为第一阶段 LLM 服务；若 GPU 后端不稳定，再评估 llama.cpp CUDA。
- 模型、缓存和工作空间优先放置在 NVMe。

### 3.3 经验资源预算

| **资源项**        | **经验范围**    | **说明**                             |
|-------------------|-----------------|--------------------------------------|
| 系统 + ROS 2      | 约 1.5–2.5 GB   | 取决于桌面环境、后台服务和节点数量   |
| SenseVoiceSmall   | 约 0.5–1.5 GB   | 与推理后端、缓存和精度有关           |
| Qwen2.5 1.5B 4bit | 约 1.5–2.5 GB   | 包含模型、KV Cache和运行时缓冲       |
| Piper TTS         | 通常低于 0.5 GB | 当前代码 use_cuda=False，主要使用CPU |
| 安全余量          | 至少 1.5 GB     | 防止峰值、音频缓存和进程抖动         |

> **说明：**以上是工程估算，不是固定值。部署时以 tegrastats、free -h 和实际峰值为准。

## 4. 模型选型

| **环节** | **推荐模型**               | **运行位置**  | **选择理由**                           |
|----------|----------------------------|---------------|----------------------------------------|
| ASR      | SenseVoiceSmall            | Jetson本地    | 中文短句识别、模型轻量、可复用现有接口 |
| LLM      | Qwen2.5 1.5B Instruct 4bit | Ollama本地    | 8GB内存下性能、效果与稳定性较均衡      |
| LLM备选  | Qwen2.5 3B Instruct 4bit   | Ollama本地    | 效果更好，但需降低上下文并验证内存峰值 |
| TTS      | Piper zh_CN-huayan-medium  | Jetson本地CPU | 部署简单、无需网络、现有代码已支持     |

### 4.1 不建议的初始配置

- Qwen2.5 7B：8GB设备上会显著压缩系统和ASR/TTS的内存余量。
- 视觉模型或视频模型：推理成本高于纯文本语音助手。
- 多个模型常驻并发：容易造成内存峰值、上下文切换和响应抖动。
- 长上下文和长回复：会增加KV Cache、首字延迟和TTS等待时间。

## 5. 配置文件

### 5.1 yahboom.yaml

```yaml
asr:
  ros__parameters:
    VAD_MODE: 2
    sample_rate: 16000
    frame_duration_ms: 30
    use_oline_asr: false
    mic_serial_port: "/dev/ttyUSB0"
    mic_index: 0
    language: "zh"
    regional_setting: "China"

model_service:
  ros__parameters:
    language: "zh"
    useolinetts: false
    llm_platform: "ollama"
    regional_setting: "China"
```

> **参数拼写：**现有代码使用 use_oline_asr 和 useolinetts。即使拼写不规范，也应与代码保持一致，不能直接改成新名称而不同时修改代码。

### 5.2 large_model_interface.yaml

```yaml
# 本地大模型
ollama_host: "http://127.0.0.1:11434"
ollama_model: "qwen2.5:1.5b"
# 本地ASR
local_asr_model: "/home/jetson/yahboom_ws/src/largemodel/MODELS/asr/SenseVoiceSmall"
# 本地中文TTS
zh_tts_model: "/home/jetson/yahboom_ws/src/largemodel/MODELS/tts/zh/zh_CN-huayan-medium.onnx"
zh_tts_json: "/home/jetson/yahboom_ws/src/largemodel/MODELS/tts/zh/zh_CN-huayan-medium.onnx.json"
```

### 5.3 推荐推理参数

| **参数**   | **初始值**     | **调整原则**                       |
|------------|----------------|------------------------------------|
| 上下文长度 | 2048 tokens    | 稳定后可试4096；内存不足则降到1024 |
| 最大输出   | 128–256 tokens | 语音回复以三句话以内为宜           |
| 并发数     | 1              | 禁止同时运行多个LLM请求            |
| 对话历史   | 最近3–5轮      | 不要无限追加messages               |
| 温度       | 0.3–0.7        | 机器人控制类指令采用较低温度       |

## 6. 代码修正与优化

### 6.1 修正短命令误判

**现有判断：**

```python
if result[0] == 'ok' and len(result[1]) > 4:
    return result[1]
```

该条件会把“停止”“前进”“左转”“回家”等正常短命令判为错误。

**建议修改：**

```python
text = result[1].strip()
if result[0] == "ok" and text:
    return text

self.get_logger().error(f"ASR Error: {result[1]}")
return "error"
```

### 6.2 修正 VAD 静音时长

当前代码设置 MAX_SILENCE_FRAMES = 90，帧长为30 ms，实际结束静音时间为 90 × 30 ms = 2.7 秒；但代码注释写的是900 ms。

```python
# 希望静音约900 ms后结束录音
MAX_SILENCE_FRAMES = 30
```

> **建议：**机器人短命令可先使用 700–1000 ms；嘈杂环境可提高到 1200–1500 ms。不要只修改注释，必须让参数与目标体验一致。

### 6.3 限制对话历史

```python
MAX_HISTORY_MESSAGES = 8
if len(self.messages) > MAX_HISTORY_MESSAGES:
    self.messages = self.messages[-MAX_HISTORY_MESSAGES:]
```

### 6.4 控制语音回复长度

```text
你是机器人离线语音助手。
默认使用简洁中文回答，除非用户要求详细说明，
否则回答控制在三句话以内。
```

### 6.5 严格串行执行

```text
录音结束
→ ASR推理
→ 释放音频缓冲
→ LLM推理
→ TTS合成
→ 播放完成
→ 恢复监听
```

## 7. 安装、部署与测试

### 7.1 环境检查

```bash
free -h
cat /etc/nv_tegra_release
dpkg-query --show nvidia-jetpack
nvcc --version
arecord -l
aplay -l
```

### 7.2 音频设备测试

```bash
arecord -D plughw:0,0 \
  -f S16_LE \
  -r 16000 \
  -c 1 \
  -d 5 \
  /tmp/mic_test.wav

aplay /tmp/mic_test.wav
```

### 7.3 Ollama 与模型测试

```bash
ollama pull qwen2.5:1.5b
ollama run qwen2.5:1.5b
```

```bash
curl http://127.0.0.1:11434/api/chat \
  -d '{
    "model": "qwen2.5:1.5b",
    "stream": false,
    "messages": [
      {"role": "user", "content": "请用一句话介绍你自己"}
    ]
  }'
```

### 7.4 分阶段测试

| **阶段** | **启动命令**                                        | **观察项**                       | **通过标准**                 |
|----------|-----------------------------------------------------|----------------------------------|------------------------------|
| ASR单测  | ros2 launch largemodel asr_only.launch.py           | /asr话题、识别文本、录音结束时间 | 短命令可识别，无网络也可运行 |
| TTS单测  | ros2 launch largemodel tts_only.launch.py           | WAV生成、扬声器输出              | 中文播报清晰，无阻塞         |
| LLM单测  | ollama run qwen2.5:1.5b                             | 首字延迟、内存、GPU使用          | 连续问答稳定，不触发OOM      |
| 集成测试 | ros2 launch largemodel largemodel_control.launch.py | 端到端时延、重复唤醒、日志       | 完成听—想—说闭环             |

### 7.5 关键测试命令

```bash
# 查看 ASR 结果
ros2 topic echo /asr

# 发布 TTS 测试文本
ros2 topic pub --once /tts_text_input std_msgs/msg/String \
  '{data: "本地语音合成测试成功"}'

# 监控资源
tegrastats
free -h

# 重新编译
cd ~/yahboom_ws
colcon build --symlink-install
source install/setup.bash
```

## 8. 性能优化与稳定性策略

- 将桌面无关应用、浏览器和多余后台服务关闭，给模型留出稳定内存。
- 使用 NVMe 存储模型和缓存，避免 microSD 高频读写。
- 限制上下文和最大输出，减少KV Cache和TTS等待。
- 避免连续重复加载模型；服务启动时初始化并保持单实例。
- 使用 tegrastats 观察 RAM、SWAP、GPU、温度和降频状态。
- 设备高温或电源不足时，优先解决散热与供电，不要仅通过降低模型掩盖问题。
- 发生内存不足时，按顺序执行：降低上下文 → 降低输出长度 → 3B降到1.5B → 关闭桌面服务。

## 9. 常见故障排查

| **现象**                | **可能原因**                       | **处理方法**                        |
|-------------------------|------------------------------------|-------------------------------------|
| 说话后等待很久才识别    | MAX_SILENCE_FRAMES=90导致2.7秒静音 | 改为30左右并重新测试                |
| “停止/前进”识别后被报错 | 识别结果长度必须大于4              | 改为空文本判断或长度≥2              |
| ASR有日志但LLM不响应    | 话题名称接错                       | 确认发布和订阅均为 /asr             |
| Ollama使用CPU、响应很慢 | GPU后端未生效或版本不兼容          | 查看 ollama ps、日志和 tegrastats   |
| 系统进程被杀或卡死      | 模型/上下文过大、并发运行          | 使用1.5B、上下文2048、并发1         |
| TTS生成成功但无声音     | 输出设备或Pulse/ALSA设置错误       | 先使用aplay验证设备和音量           |
| 开机服务无法播放声音    | systemd缺少音频会话环境            | 手动运行稳定后再配置systemd环境变量 |

## 10. 验收标准

- [ ] 拔掉网络后，ASR、LLM、TTS仍能完成完整交互。
- [ ] “停止、前进、左转、回家”等2–4字短命令能够正常进入LLM或控制逻辑。
- [ ] 普通话短句识别完成后，静音等待时间约为1秒，而不是2.7秒。
- [ ] 连续进行20轮语音问答，无进程退出、无OOM、无明显内存持续增长。
- [ ] LLM回复默认不超过三句话，TTS可完整播报。
- [ ] ROS 2实际话题为 /asr，发布端与订阅端一致。
- [ ] tegrastats显示系统保留合理内存余量，温度与频率无异常。
- [ ] 重启程序后模型、音频设备和ROS节点能够稳定初始化。

## 11. 实施顺序

1.  确认 JetPack、ROS 2、CUDA、麦克风和扬声器。
2.  部署并单独测试 SenseVoiceSmall。
3.  部署并单独测试 Piper。
4.  安装 Ollama，运行 Qwen2.5 1.5B。
5.  修改 YAML 为全离线配置。
6.  修正短命令判断和VAD静音帧数。
7.  分阶段运行 ASR、TTS、LLM 单测。
8.  启动完整 ROS 2 语音交互链路。
9.  连续压测并记录 tegrastats。
10. 所有人工启动测试通过后，再配置 systemd 开机自启动。

## 12. 最终推荐配置清单

| **项目** | **推荐值**                                 |
|----------|--------------------------------------------|
| 硬件     | Jetson Orin Nano 8GB + NVMe + 主动散热     |
| 系统     | JetPack 6.x / Ubuntu 22.04 / ROS 2 Humble  |
| ASR      | SenseVoiceSmall，16kHz，WebRTC VAD         |
| LLM      | Qwen2.5 1.5B Instruct 4bit；稳定后可尝试3B |
| TTS      | Piper zh_CN-huayan-medium，CPU执行         |
| 上下文   | 2048 tokens                                |
| 输出长度 | 128–256 tokens，默认三句话以内             |
| 并发     | 1，ASR→LLM→TTS串行                         |
| ROS话题  | /asr                                       |
| 在线能力 | 关闭，不配置云端回退                       |

## 附录 A：systemd 启动前检查

> **原则：**只有手动启动完整链路稳定后，才配置开机自启动。否则模型、音频设备或ROS环境错误会被自动重启掩盖。

```bash
#!/bin/bash
source /opt/ros/humble/setup.bash
source /home/jetson/yahboom_ws/install/setup.bash
exec ros2 launch largemodel largemodel_control.launch.py
```

```bash
sudo systemctl daemon-reload
sudo systemctl enable largemodel.service
sudo systemctl start largemodel.service
sudo systemctl status largemodel.service
journalctl -u largemodel.service -f
```
