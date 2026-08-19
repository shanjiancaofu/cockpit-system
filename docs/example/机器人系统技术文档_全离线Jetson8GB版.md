# 机器人系统技术文档

> 文档类型：系统设计、软件架构、接口、部署与测试说明  
> 适用项目：移动机器人  
> AI计算平台基线：NVIDIA Jetson Orin Nano 8GB  
> 语音方案基线：全离线语音交互  
> 文档状态：工程整合版

---

## 1. 文档目的与适用范围

本文档用于描述机器人系统的系统架构、硬件组成、嵌入式控制、ROS软件、传感器、导航、视觉算法、端侧大模型、离线语音交互、系统优化和部署方法。

本文档不再以课程章节、学习目标和实践作业为组织方式，而是按照工程技术文档的形式组织，主要面向以下工作：

- 系统方案设计与评审；
- 软件模块开发与接口联调；
- Jetson Orin Nano 8GB端侧模型部署；
- ROS节点、话题、参数和启动文件维护；
- 性能测试、故障定位和稳定性验证；
- 版本交付、自动启动和容器化部署。

本文档中的离线语音交互限定为：

```text
WebRTC VAD
→ SenseVoiceSmall离线ASR
→ 本地Qwen语言模型
→ Piper离线TTS
```

当前阶段不使用在线ASR、在线TTS或云端LLM作为默认后端。

---

## 2. 系统目标

### 2.1 功能目标

机器人系统需要提供以下能力：

1. 双轮差速底盘运动控制；
2. STM32实时电机控制、编码器反馈和安全保护；
3. 激光雷达、相机和里程计数据采集；
4. ROS节点通信、TF坐标管理和状态发布；
5. SLAM建图、AMCL定位、路径规划和动态避障；
6. V4L2相机采集、硬件解码、目标检测和目标跟踪；
7. Jetson端本地大语言模型推理；
8. 全离线语音识别、语言理解和语音合成；
9. Function Call或工具调用驱动机器人动作；
10. 系统性能监控、日志管理、自动启动和容器化部署。

### 2.2 非功能目标

系统设计应满足以下要求：

- 控制链路与AI推理解耦；
- STM32侧控制周期稳定，不依赖上位机推理速度；
- 断网时基本语音交互、导航和底盘控制仍可运行；
- 音频、对话和传感器数据默认不上传到云端；
- AI推理采用单任务或受控并发，避免8GB共享内存耗尽；
- 关键节点支持异常退出检测和自动恢复；
- 配置项集中管理，避免代码内硬编码设备号和模型路径；
- 关键接口具备可观测日志、超时和错误返回。

---

## 3. 系统总体架构

### 3.1 硬件分层

系统采用“高性能计算平台 + 实时控制器”的分层架构。

```text
┌──────────────────────────────────────────────┐
│ Jetson Orin Nano 8GB                         │
│ ROS 2、导航、视觉、ASR、LLM、TTS、Agent      │
└──────────────────────┬───────────────────────┘
                       │ UART / USB / ROS接口
┌──────────────────────▼───────────────────────┐
│ STM32F103实时控制器                           │
│ 电机PWM、编码器、PID、心跳、安全保护          │
└──────────────────────┬───────────────────────┘
                       │
             ┌─────────▼─────────┐
             │ 电机、雷达、相机等 │
             └───────────────────┘
```

职责划分如下：

| 模块 | 主要职责 |
|---|---|
| Jetson Orin Nano 8GB | ROS 2节点、导航、感知、AI推理、语音交互、系统管理 |
| STM32F103 | 20ms实时控制、编码器读取、PWM输出、串口协议、失联保护 |
| 激光雷达 | 提供二维环境距离数据 |
| USB相机 | 提供图像输入 |
| 麦克风 | 提供16kHz单声道语音输入 |
| 扬声器 | 播放提示音和Piper合成语音 |
| 电机与编码器 | 执行运动并返回轮速、位移反馈 |

### 3.2 软件分层

```text
应用层
├─ 离线语音助手
├─ 导航任务
├─ 目标跟踪
└─ Agent工具调用

AI与算法层
├─ SenseVoiceSmall
├─ Qwen2.5本地模型
├─ Piper
├─ YOLO/RKNN或Jetson推理后端
├─ SLAM / AMCL
└─ 路径规划器

ROS中间件层
├─ ROS 2节点
├─ Topic / Service / Action
├─ TF
├─ 参数系统
└─ rosbag与诊断

设备与驱动层
├─ V4L2相机
├─ YDLidar
├─ ALSA/PyAudio
├─ UART
└─ GPIO/PWM

实时控制层
└─ STM32电机、编码器、PID和安全保护
```

### 3.3 ROS1/ROS2兼容策略

原项目部分模块基于Ubuntu 20.04和ROS Noetic，离线语音模块及Jetson部署基线采用Ubuntu 22.04和ROS 2 Humble。工程上不得在文档中将两套环境混写为同一个运行环境。

推荐策略：

- 新增和重构模块优先使用ROS 2 Humble；
- Jetson Orin Nano上的离线语音、LLM和新AI节点使用ROS 2；
- 旧ROS1模块未迁移前，可通过`ros1_bridge`、串口协议或自定义网桥进行连接；
- 同一个工作空间不得同时使用`catkin_make`和`colcon build`；
- 每个部署包需要明确操作系统、ROS版本和依赖版本。

---

## 4. 硬件系统设计

### 4.1 Jetson Orin Nano 8GB

Jetson Orin Nano 8GB作为端侧AI和ROS计算平台，负责：

- SenseVoiceSmall语音识别；
- Qwen2.5量化语言模型推理；
- Piper语音合成；
- ROS 2节点管理；
- 导航、视觉和工具调用；
- 日志、监控与部署管理。

推荐存储结构：

```text
/home/jetson/yahboom_ws              ROS 2工作空间
/home/jetson/models                  通用模型目录
/home/jetson/yahboom_ws/src/largemodel/MODELS
                                     项目内模型目录
/var/lib/ollama                      Ollama模型与缓存
/home/jetson/logs                    项目日志
```

推荐使用NVMe作为模型、工作空间、日志和缓存盘。microSD仅适合作为系统启动介质或低频数据存储，不建议承担大模型频繁读取和交换空间写入。

### 4.2 STM32F103控制器

STM32侧承担硬实时任务，避免上位机AI推理阻塞影响底盘控制。

主要功能：

- TIM1输出约10kHz PWM；
- TIM2和TIM4工作于正交编码器模式；
- 20ms周期读取编码器并执行PID；
- USART3与Jetson或上位机通信；
- 接收速度或轮速目标；
- 上传编码器、轮速和状态；
- 心跳超时后自动将PWM置零。

### 4.3 传感器与执行器

| 设备 | 接口 | 主要数据 |
|---|---|---|
| YDLidar | USB/UART | `sensor_msgs/LaserScan` |
| USB相机 | V4L2 | MJPEG/YUYV图像 |
| 编码器 | STM32定时器 | 左右轮脉冲 |
| 麦克风 | USB/ALSA/PyAudio | 16kHz、16bit、单声道PCM |
| 扬声器 | ALSA | WAV音频 |
| 双轮电机 | TB6612/PWM | 左右轮PWM和方向 |

---

## 5. STM32实时控制系统

### 5.1 差速轮运动学

设左右轮线速度分别为`v_l`和`v_r`，轮距为`L`，则机器人线速度和角速度为：

```text
v = (v_r + v_l) / 2
ω = (v_r - v_l) / L
```

逆解为：

```text
v_l = v - ωL / 2
v_r = v + ωL / 2
```

编码器脉冲需要结合轮径、减速比和每转脉冲数换算为轮子位移。位姿累计应基于固定控制周期或时间戳计算。

### 5.2 PID控制

左右轮分别维护独立PID状态：

```text
error
integral
previous_error
output_limit
```

设计要求：

- 左右轮不得共享积分项；
- 方向切换时清空积分和上一次误差；
- PWM输出执行限幅；
- PID参数支持配置或动态调整；
- 编码器异常时停止对应电机；
- 控制循环内避免动态内存分配和阻塞操作。

### 5.3 控制周期与心跳保护

建议使用20ms控制周期：

```text
读取编码器
→ 计算实际速度
→ 执行PID
→ 更新方向GPIO与PWM
→ 上传状态
```

每次收到有效控制命令时清零心跳计数。连续5个周期未收到命令，即约100ms后，将左右轮PWM置零。

### 5.4 串口协议

串口数据结构使用1字节对齐，避免编译器填充。

推荐帧结构：

```text
帧头 | 类型 | 长度 | 序号 | 数据区 | 校验 | 帧尾
```

协议应包含：

- 固定帧头和帧尾；
- 数据长度；
- 命令或状态类型；
- 序号；
- 校验字段；
- 超时处理；
- 缓冲区溢出保护；
- 错帧丢弃与重新同步。

---

## 6. ROS底盘与设备层

### 6.1 `base_control`节点

主要职责：

- 订阅`cmd_vel`；
- 将线速度和角速度转换为左右轮目标；
- 通过串口发送控制包；
- 接收编码器和状态包；
- 计算里程计；
- 发布`odom`；
- 发布`odom -> base_footprint` TF；
- 处理串口断开、超时和重连。

### 6.2 里程计

里程计消息至少应包含：

- 位置`x、y`；
- 姿态四元数；
- 线速度；
- 角速度；
- 位姿协方差；
- 速度协方差；
- 时间戳；
- `frame_id=odom`；
- `child_frame_id=base_footprint`。

### 6.3 USB相机驱动

相机驱动基于V4L2，推荐能力：

- 支持1280×720@30fps；
- 使用MMAP多缓冲队列；
- 支持MJPEG采集；
- 使用`select()`等待设备可读；
- 配置读取超时；
- 无订阅者时停止采集；
- 采集异常后释放并重连；
- 支持动态降采样；
- 统一处理时间戳和`frame_id`。

### 6.4 激光雷达

YDLidar驱动层应抽象为统一雷达接口，便于扩展RPLidar或LDLidar。

配置项应包括：

- 串口路径；
- 波特率；
- 扫描方向；
- 角度范围；
- 距离范围；
- 采样频率；
- 坐标系名称；
- 异常重连策略。

### 6.5 TF坐标树

推荐坐标树：

```text
map
└─ odom
   └─ base_footprint
      └─ base_link
         ├─ laser_link
         ├─ camera_link
         ├─ imu_link
         └─ microphone_link
```

静态传感器外参应由URDF或静态TF统一发布，禁止多个节点重复发布同一TF。

---

## 7. 导航系统

### 7.1 SLAM建图

系统可使用Gmapping或ROS 2对应SLAM方案完成二维建图。输入包括：

- 激光雷达数据；
- 里程计；
- TF；
- 时间戳一致的数据流。

地图输出需要保存栅格地图文件和配套YAML元数据。

### 7.2 AMCL定位

AMCL通过粒子滤波融合激光雷达与里程计。关键配置包括：

- 粒子数上下限；
- 里程计运动模型；
- 激光模型；
- 更新距离和角度阈值；
- 初始位姿；
- TF容忍时间。

### 7.3 路径规划

全局规划可使用A*或Dijkstra；局部规划可使用DWA、TEB或ROS 2 Nav2对应插件。

代价地图建议区分：

- 全局静态地图；
- 局部滚动窗口；
- 障碍层；
- 膨胀层；
- 机器人外形或半径。

原项目参考参数：

```yaml
obstacle_range: 3.0
raytrace_range: 3.5
robot_radius: 0.1
inflation_radius: 0.2
```

实际部署时必须根据机器人尺寸、雷达安装位置和运动速度重新标定。

### 7.4 导航任务接口

导航控制模块应采用Action接口异步执行，支持：

- 发送目标点；
- 查询状态；
- 取消任务；
- 超时；
- 失败重试；
- 预定义地点；
- 语音指令映射为导航目标。

---

## 8. 计算机视觉与目标跟踪

### 8.1 图像预处理

原RK平台方案使用RK-MPP进行JPEG硬解码、RGA进行硬件缩放；Jetson平台可替换为NVJPEG、GStreamer、CUDA或TensorRT流水线。

接口层应保持一致：

```text
压缩图像
→ 解码
→ RGB/BGR格式统一
→ 缩放与填充
→ 模型输入
```

### 8.2 目标检测

原项目采用YOLOv6-N与RKNN NPU。迁移到Jetson后可使用TensorRT或其他CUDA推理后端，但检测输出结构应统一：

```text
class_id
score
x_min
y_min
x_max
y_max
timestamp
frame_id
```

### 8.3 多线程流水线

推荐线程或任务划分：

```text
采集线程
→ 输入队列
→ 推理线程
→ 结果队列
→ 后处理/发布线程
```

队列应设置最大长度，发生积压时优先丢弃旧帧，避免延迟不断累积。

### 8.4 目标跟踪与雷达融合

目标跟踪逻辑：

1. 按指定类别过滤检测框；
2. 选择面积最大或置信度最高目标；
3. 计算目标中心偏差；
4. 将雷达点投影到图像；
5. 获取检测框内有效距离；
6. 生成线速度和角速度；
7. 达到安全距离后停止。

---

## 9. Jetson Orin Nano 8GB全离线语音系统

### 9.1 设计边界

本项目当前只实现全离线语音链路，不考虑在线ASR、在线TTS和云端LLM。

固定数据流：

```text
麦克风
→ WebRTC VAD
→ WAV录音
→ SenseVoiceSmall
→ ROS 2 /asr
→ model_service
→ 本地Qwen2.5
→ Piper
→ WAV
→ 扬声器
```

核心要求：

- 语音数据不上传；
- 网络断开后仍可识别、推理和合成；
- ASR、LLM、TTS默认串行执行；
- 限制上下文和回复长度；
- 禁止默认加载7B模型和多模态模型；
- 模型与ROS进程总内存不得触发系统OOM。

### 9.2 模型选型

#### 9.2.1 ASR

默认模型：

```text
SenseVoiceSmall
```

用途：

- 中文短句识别；
- 机器人控制命令；
- 日常语音问答；
- 中英文混合短句。

#### 9.2.2 LLM

默认模型：

```text
Qwen2.5 1.5B Instruct，4bit量化
```

可选升级：

```text
Qwen2.5 3B Instruct，4bit量化
```

不建议作为初始配置：

```text
Qwen2.5 7B
LLaVA
视频理解模型
大型自主Agent模型
```

8GB内存需要同时容纳操作系统、ROS 2、ASR、LLM、TTS、音频缓冲和其他节点，因此模型选择必须以稳定性优先。

#### 9.2.3 TTS

默认模型：

```text
Piper
zh_CN-huayan-medium.onnx
```

Piper默认使用CPU推理，避免与LLM争用GPU。

### 9.3 运行时资源策略

建议配置：

| 参数 | 推荐值 |
|---|---|
| LLM | Qwen2.5 1.5B 4bit |
| 上下文长度 | 2048 |
| 最大输出 | 128～256 tokens |
| 对话历史 | 最近3～5轮 |
| 并发请求 | 1 |
| ASR采样率 | 16000Hz |
| 音频声道 | 单声道 |
| VAD帧长 | 30ms |
| TTS | Piper medium |
| 模型存储 | NVMe |

执行顺序：

```text
允许录音
→ VAD判定结束
→ 关闭录音流
→ ASR推理
→ LLM推理
→ TTS合成
→ 播放
→ 播放完成
→ 恢复录音
```

TTS播放期间必须暂停或屏蔽ASR，避免扬声器声音被麦克风再次识别。

---

## 10. 离线ASR设计

### 10.1 节点职责

`largemodel/largemodel/asr.py`中的`ASRNode`负责：

- 初始化麦克风；
- 初始化WebRTC VAD；
- 监听唤醒事件；
- 录制有效语音；
- 保存WAV文件；
- 调用SenseVoiceSmall；
- 发布识别结果。

### 10.2 VAD录音流程

音频格式：

```text
采样率：16000Hz
采样位宽：16bit
声道：1
帧长：30ms
```

流程：

```text
打开PyAudio输入流
→ 逐帧读取PCM
→ vad.is_speech()
→ 检测到人声后开始缓存
→ 连续静音达到阈值
→ 结束录音
→ 去除尾部静音
→ 保存WAV
```

原实现中存在注释与参数不一致问题：

```python
MAX_SILENCE_FRAMES = 90
frame_duration_ms = 30
```

90帧对应约2.7秒，而不是注释中的900ms。若目标静音结束时间为900ms，应设置为：

```python
MAX_SILENCE_FRAMES = 30
```

建议将静音时长改为参数：

```yaml
max_silence_ms: 600
```

并由程序计算：

```python
max_silence_frames = max_silence_ms // frame_duration_ms
```

### 10.3 ASR后端

全离线配置下只调用：

```python
self.modelinterface.SenseVoiceSmall_ASR(input_file)
```

配置参数`use_oline_asr`保留原项目拼写，以兼容现有代码，但固定设置为`false`。

### 10.4 短命令过滤缺陷

原实现使用：

```python
if result[0] == "ok" and len(result[1]) > 4:
```

该逻辑会误判以下正常指令：

```text
停止
前进
后退
左转
右转
回家
开灯
```

建议修改为：

```python
def ASR_conversion(self, input_file: str) -> str:
    result = self.modelinterface.SenseVoiceSmall_ASR(input_file)
    if result[0] != "ok":
        self.get_logger().error(f"ASR Error: {result[1]}")
        return "error"

    text = result[1].strip()
    if not text:
        self.get_logger().warning("ASR returned empty text")
        return "error"

    return text
```

若需要过滤单字符噪声，应使用置信度、词表或命令语法进行判断，不应简单按字符数过滤。

### 10.5 ROS接口

实际代码发布话题为：

```text
/asr
```

消息类型：

```text
std_msgs/msg/String
```

发布端：

```python
self.asr_pub = self.create_publisher(String, "asr", 5)
```

订阅端：

```python
self.asrsub = self.create_subscription(
    String,
    "asr",
    self.asr_callback,
    1
)
```

工程文档和架构图统一使用`/asr`，不再使用与代码不一致的`/asr_text`。

---

## 11. 本地LLM服务

### 11.1 服务职责

`model_service.py`订阅`/asr`，完成：

- 文本预处理；
- 对话上下文管理；
- 本地模型推理；
- JSON或文本结果解析；
- Function Call解析；
- TTS调用；
- 工具执行；
- 异常恢复。

### 11.2 Ollama后端

默认地址：

```yaml
ollama_host: "http://127.0.0.1:11434"
ollama_model: "qwen2.5:1.5b"
```

模型安装：

```bash
ollama pull qwen2.5:1.5b
```

独立测试：

```bash
ollama run qwen2.5:1.5b
```

HTTP测试：

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

运行时检查：

```bash
ollama ps
tegrastats
journalctl -u ollama -f
```

### 11.3 llama.cpp备选后端

Ollama无法稳定调用Jetson GPU时，可将LLM后端替换为`llama.cpp`。

建议接口保持OpenAI兼容或封装统一方法：

```python
infer_with_text(text, message)
```

切换后端时，`model_service`不应修改业务逻辑，只修改模型接口层。

### 11.4 上下文管理

不得无限追加历史消息。建议：

```python
MAX_HISTORY_MESSAGES = 8

if len(self.messages) > MAX_HISTORY_MESSAGES:
    self.messages = self.messages[-MAX_HISTORY_MESSAGES:]
```

系统提示词建议约束回复：

```text
你是本机器人的本地语音助手。
默认使用简洁中文回答。
除非用户明确要求详细说明，否则控制在三句话内。
机器人控制命令必须输出符合工具接口定义的结构化参数。
```

### 11.5 Function Call

工具调用应采用结构化结果，不允许直接执行模型生成的任意Shell命令。

推荐返回格式：

```json
{
  "response": "正在前往客厅。",
  "tools": [
    {
      "name": "navigate_to",
      "arguments": {
        "location": "living_room"
      }
    }
  ]
}
```

执行器必须：

- 使用函数白名单；
- 校验参数类型和范围；
- 对运动类命令增加安全状态检查；
- 支持超时和取消；
- 记录调用日志；
- 禁止`eval()`或任意代码执行。

---

## 12. 离线TTS设计

### 12.1 初始化

`model_service.py`读取：

```text
useolinetts
```

全离线模式固定为：

```yaml
useolinetts: false
```

本地初始化：

```python
self.model_client.tts_model_init("local", self.language)
```

Piper模型加载：

```python
self.synthesizer = piper.PiperVoice.load(
    tts_model,
    config_path=tts_json,
    use_cuda=False
)
```

### 12.2 合成与播放

调用链：

```text
process_model_result()
→ _safe_play_audio()
→ voice_synthesis()
→ 生成tts_output.wav
→ play_audio_async()
```

完整语音交互中，`model_service`可以直接执行合成和播放，不要求另设`/tts_text`话题。`tts_only.launch.py`和`/tts_text_input`用于独立测试。

### 12.3 模型路径

```yaml
zh_tts_model: "/home/jetson/yahboom_ws/src/largemodel/MODELS/tts/zh/zh_CN-huayan-medium.onnx"
zh_tts_json: "/home/jetson/yahboom_ws/src/largemodel/MODELS/tts/zh/zh_CN-huayan-medium.onnx.json"
```

### 12.4 播放互斥

语音播放需要与录音互斥：

```text
TTS开始
→ 设置speaking状态
→ 暂停ASR或忽略麦克风输入
→ 播放完成
→ 清理输出文件
→ 恢复ASR
```

音频播放接口应防止多个回复同时播放，并支持中断上一条低优先级播报。

---

## 13. ROS 2配置

### 13.1 `yahboom.yaml`

```yaml
asr:
  ros__parameters:
    VAD_MODE: 2
    sample_rate: 16000
    frame_duration_ms: 30
    max_silence_ms: 600

    # 保留原项目参数名，固定使用离线ASR
    use_oline_asr: false

    mic_serial_port: "/dev/ttyUSB0"
    mic_index: 0
    language: "zh"
    regional_setting: "China"

model_service:
  ros__parameters:
    language: "zh"

    # 保留原项目参数名，固定使用离线TTS
    useolinetts: false

    llm_platform: "ollama"
    regional_setting: "China"

    max_history_messages: 8
    max_output_tokens: 256
    text_chat_mode: false
```

注意：`max_silence_ms`、`max_history_messages`和`max_output_tokens`若当前代码尚未声明，需要同步修改节点参数声明。

### 13.2 `large_model_interface.yaml`

```yaml
ollama_host: "http://127.0.0.1:11434"
ollama_model: "qwen2.5:1.5b"

local_asr_model: "/home/jetson/yahboom_ws/src/largemodel/MODELS/asr/SenseVoiceSmall"

zh_tts_model: "/home/jetson/yahboom_ws/src/largemodel/MODELS/tts/zh/zh_CN-huayan-medium.onnx"
zh_tts_json: "/home/jetson/yahboom_ws/src/largemodel/MODELS/tts/zh/zh_CN-huayan-medium.onnx.json"

en_tts_model: "/home/jetson/yahboom_ws/src/largemodel/MODELS/tts/en/en_US-libritts-high.onnx"
en_tts_json: "/home/jetson/yahboom_ws/src/largemodel/MODELS/tts/en/en_US-libritts-high.onnx.json"
```

### 13.3 参数命名风险

原项目中存在以下拼写：

```text
use_oline_asr
useolinetts
```

在未同步修改代码、YAML和启动文件前，不得自行更名为：

```text
use_online_asr
use_online_tts
```

否则节点会使用默认值或无法加载参数。

---

## 14. 构建与运行环境

### 14.1 Jetson软件基线

推荐：

```text
JetPack 6.x
Ubuntu 22.04
ROS 2 Humble
Python 3.10
CUDA、cuDNN、TensorRT使用JetPack配套版本
```

旧RK/ROS1模块继续使用独立环境：

```text
Ubuntu 20.04
ROS Noetic
```

两套环境的依赖、工作空间和启动脚本应分开管理。

### 14.2 ROS 2工作空间构建

```bash
cd ~/yahboom_ws
source /opt/ros/humble/setup.bash

colcon build --symlink-install

source install/setup.bash
```

发布版本可去除`--symlink-install`，但开发阶段建议保留。

### 14.3 环境检查

```bash
free -h
cat /etc/nv_tegra_release
dpkg-query --show nvidia-jetpack
nvcc --version
python3 --version
ros2 --help
```

音频检查：

```bash
arecord -l
aplay -l
```

串口检查：

```bash
ls -l /dev/ttyUSB*
dmesg | tail -n 50
```

---

## 15. 分阶段测试

完整系统启动前，必须分别验证麦克风、ASR、LLM、TTS和ROS数据流。

### 15.1 音频设备测试

录音：

```bash
arecord -D plughw:0,0 \
  -f S16_LE \
  -r 16000 \
  -c 1 \
  -d 5 \
  /tmp/mic_test.wav
```

播放：

```bash
aplay /tmp/mic_test.wav
```

验收条件：

- 录音无明显削波；
- 声道和采样率正确；
- 无持续底噪或设备断开；
- 扬声器输出设备正确。

### 15.2 ASR独立测试

```bash
cd ~/yahboom_ws
source /opt/ros/humble/setup.bash
source install/setup.bash

ros2 launch largemodel asr_only.launch.py
```

另开终端：

```bash
source /opt/ros/humble/setup.bash
source ~/yahboom_ws/install/setup.bash

ros2 topic echo /asr
```

测试语句：

```text
你好
停止
向前走
左转
回到充电位置
介绍一下你自己
```

验收条件：

- 每次说话后生成有效WAV；
- 短命令不会被长度过滤；
- 识别结果发布到`/asr`；
- 无网络连接时仍可识别；
- 空白和噪声不发布为有效命令。

### 15.3 TTS独立测试

```bash
ros2 launch largemodel tts_only.launch.py
```

另开终端：

```bash
ros2 topic pub --once \
  /tts_text_input \
  std_msgs/msg/String \
  '{data: "本地语音合成测试成功"}'
```

验收条件：

- 正确生成WAV；
- 可正常播放；
- 中文无明显乱码或读音错误；
- 连续调用不会产生文件占用冲突。

### 15.4 LLM独立测试

```bash
ollama run qwen2.5:1.5b
```

测试提示：

```text
请只用不超过五十个汉字回答：Jetson是什么？
```

同时监控：

```bash
tegrastats
free -h
```

验收条件：

- 能够稳定生成；
- 不发生OOM；
- 模型进程不频繁退出；
- 回复长度符合约束；
- 重复请求后内存无持续异常增长。

### 15.5 完整链路测试

```bash
ros2 launch largemodel largemodel_control.launch.py
```

观察：

```bash
ros2 topic echo /asr
ros2 node list
ros2 topic list
```

完整链路：

```text
唤醒
→ 录音
→ ASR文本
→ LLM回复
→ TTS播放
→ 恢复监听
```

---

## 16. 验收指标

以下指标为建议验收目标，实际数值需要在目标硬件上实测记录。

### 16.1 功能验收

| 项目 | 验收要求 |
|---|---|
| 断网运行 | 断开网络后ASR、LLM、TTS可完成一轮对话 |
| 短命令识别 | “停止、前进、左转”等可发布到`/asr` |
| TTS互斥 | 播放期间不触发自身语音识别 |
| LLM稳定性 | 连续20轮对话无OOM和进程崩溃 |
| 工具调用 | 非白名单函数不得执行 |
| 底盘安全 | 上位机断连后100ms左右停止PWM |
| 导航取消 | 可取消正在执行的导航任务 |
| 日志 | 关键错误包含节点、时间和异常信息 |

### 16.2 性能记录

建议记录：

- VAD结束等待时间；
- ASR推理时间；
- LLM首token时间；
- LLM生成速度；
- TTS合成时间；
- 从说话结束到开始播报的端到端时间；
- GPU利用率；
- CPU利用率；
- 峰值内存；
- 温度与功耗模式。

### 16.3 原项目性能数据

原项目已有部分测量结果，可作为旧平台参考，不应直接视为Jetson版本实测结果：

| 模块 | 原项目数据 |
|---|---|
| STM32控制周期 | 20ms |
| 心跳停止时间 | 约100ms |
| RK-MPP硬解码 | 约2ms |
| X86 OpenCV软解码 | 约30ms |
| RKNN INT8推理 | 小于约27ms |
| 视觉端到端链路 | 小于约60ms |
| ROS共享内存平均时延 | 约11.46ms |
| ROS-TCP平均时延 | 约16.78ms |
| ROS2 Best Effort平均时延 | 约16.68ms |
| ROS2 Reliable平均时延 | 约33.86ms |

迁移到Jetson后必须重新测试，不得直接沿用上述数据作为交付指标。

---

## 17. ROS通信优化

### 17.1 通信方式

系统可根据数据类型选择：

| 数据 | 推荐方式 |
|---|---|
| 控制命令 | 小消息、可靠QoS |
| 状态与里程计 | 可靠或按需求配置 |
| 相机图像 | Best Effort、SensorDataQoS |
| 激光雷达 | SensorDataQoS |
| 大块自定义数据 | 共享内存或零拷贝 |
| 任务状态 | Action |
| 配置变更 | 参数或Service |

### 17.2 共享内存

大带宽数据可采用共享内存存储实际数据，ROS消息只传输句柄或引用。

共享内存模块需要处理：

- 内存生命周期；
- 引用计数；
- 互斥；
- 异常进程清理；
- 消息类型检查；
- 内存不足淘汰策略；
- 订阅者晚启动；
- 版本兼容。

### 17.3 QoS配置

语音文本、工具调用和控制命令不宜使用与图像相同的QoS。

示例：

```text
/asr              Reliable，Keep Last
/cmd_vel          Reliable，Keep Last
/camera/image     Best Effort，SensorData
/scan             Best Effort或Reliable按雷达驱动选择
/diagnostics      Reliable
```

---

## 18. 系统监控与日志

### 18.1 系统资源监控

监控项：

- CPU总利用率和各核利用率；
- GPU利用率；
- 内存与交换空间；
- 温度；
- 磁盘空间与I/O；
- 网络；
- 软中断；
- 进程线程数；
- ROS节点存活状态。

Jetson监控：

```bash
tegrastats
```

### 18.2 ROS节点监控

每个关键节点应提供：

- 启动成功日志；
- 模型加载耗时；
- 当前后端；
- 输入输出计数；
- 最近错误；
- 平均处理时间；
- 队列长度；
- 重连次数。

### 18.3 日志分级

```text
DEBUG：帧级、调试细节
INFO：启动、状态切换、正常任务
WARN：可恢复异常、重试、超时
ERROR：任务失败、模型或设备异常
FATAL：系统无法继续运行
```

禁止在默认INFO级别逐帧打印VAD状态，否则会产生大量日志并影响实时性。帧级输出应改为DEBUG或按固定周期汇总。

---

## 19. 自动启动与部署

### 19.1 启动脚本

文件：

```text
/home/jetson/yahboom_ws/src/largemodel/start_largemodel.sh
```

内容：

```bash
#!/usr/bin/env bash
set -euo pipefail

source /opt/ros/humble/setup.bash
source /home/jetson/yahboom_ws/install/setup.bash

export ROS_DOMAIN_ID=0
export PYTHONUNBUFFERED=1

exec ros2 launch largemodel largemodel_control.launch.py
```

授权：

```bash
chmod +x /home/jetson/yahboom_ws/src/largemodel/start_largemodel.sh
```

### 19.2 systemd服务

文件：

```text
/etc/systemd/system/xiaomu-largemodel.service
```

内容：

```ini
[Unit]
Description=Xiaomu Offline Voice and Large Model Service
After=multi-user.target sound.target
Wants=ollama.service
After=ollama.service

[Service]
Type=simple
User=jetson
WorkingDirectory=/home/jetson/yahboom_ws
ExecStart=/home/jetson/yahboom_ws/src/largemodel/start_largemodel.sh
Restart=on-failure
RestartSec=5
TimeoutStopSec=20
Environment=HOME=/home/jetson

[Install]
WantedBy=multi-user.target
```

管理命令：

```bash
sudo systemctl daemon-reload
sudo systemctl enable xiaomu-largemodel.service
sudo systemctl start xiaomu-largemodel.service
sudo systemctl status xiaomu-largemodel.service
journalctl -u xiaomu-largemodel.service -f
```

只有在ASR、LLM、TTS和完整链路均手动测试通过后，才启用开机自启动。

### 19.3 Docker

容器化部署需要处理：

- Jetson CUDA运行时；
- USB相机；
- 麦克风和声卡；
- 串口；
- GPU设备；
- ROS网络；
- 主机时间；
- 模型目录挂载；
- 日志目录挂载。

设备映射示例需要根据实际设备调整，不建议默认使用完全特权模式。必须最小化容器权限。

---

## 20. 故障排查

### 20.1 ASR无结果

检查顺序：

```bash
arecord -l
ls -l /dev/snd
ros2 param get /asr use_oline_asr
ros2 topic echo /asr
```

可能原因：

- 麦克风索引错误；
- 采样率不支持；
- VAD静音阈值过大；
- SenseVoice模型路径错误；
- 短命令被长度过滤；
- 音频文件为空；
- 模型依赖未安装。

### 20.2 TTS无声音

检查：

```bash
aplay -l
aplay /tmp/mic_test.wav
```

可能原因：

- 默认输出设备错误；
- 系统静音；
- Piper模型路径错误；
- WAV被异步播放线程提前删除；
- 多个播放任务竞争；
- 进程无声卡访问权限。

### 20.3 LLM使用CPU或响应过慢

检查：

```bash
ollama ps
tegrastats
journalctl -u ollama -f
```

处理：

- 确认JetPack、CUDA和Ollama版本兼容；
- 降低模型到1.5B；
- 降低上下文到2048；
- 限制输出为128 tokens；
- 关闭其他GPU任务；
- 尝试llama.cpp CUDA后端。

### 20.4 系统OOM

处理顺序：

1. 将3B降为1.5B；
2. 减少上下文；
3. 清理对话历史；
4. 关闭多模态模型；
5. 严格限制并发为1；
6. 检查是否存在模型重复加载；
7. 检查Python对象、音频缓存和消息列表是否持续增长。

### 20.5 TTS声音被ASR再次识别

处理：

- 播放期间禁用录音；
- 增加播放状态锁；
- 播放完成后增加短暂冷却时间；
- 调整麦克风与扬声器位置；
- 使用回声消除；
- 过滤机器人自身播报文本。

### 20.6 ROS话题不通

```bash
ros2 node list
ros2 topic list
ros2 topic info /asr -v
ros2 topic echo /asr
```

确认：

- 发布与订阅均使用`/asr`；
- ROS_DOMAIN_ID一致；
- QoS兼容；
- 工作空间已正确`source`；
- 节点没有在回调中崩溃。

---

## 21. 安全与可靠性

### 21.1 运动安全

所有AI生成的运动指令必须经过独立安全层：

```text
LLM意图
→ 工具参数校验
→ 当前状态检查
→ 速度/角速度限幅
→ 障碍物检查
→ 下发底盘
```

LLM不得直接生成PWM值或绕过底盘控制器。

### 21.2 数据安全

全离线模式下：

- 音频文件只保存在本机；
- 临时WAV在任务完成后清理；
- 对话历史按需要持久化；
- 日志中避免记录敏感原始音频；
- 模型接口不得默认访问公网；
- API密钥配置从本版本移除或禁用。

### 21.3 异常恢复

关键节点应支持：

- 模型加载失败后明确退出；
- 音频设备断开后重试；
- 串口断开后停止底盘并重连；
- Ollama不可用时返回固定错误播报；
- TTS失败时保留文本日志；
- systemd仅对可恢复故障自动重启；
- 连续重启需要限速，避免重启风暴。

---

## 22. 版本与配置管理

### 22.1 目录建议

```text
yahboom_ws/
├─ src/
│  ├─ base_control/
│  ├─ camera_driver/
│  ├─ lidar_driver/
│  ├─ navigation/
│  ├─ vision/
│  └─ largemodel/
│     ├─ config/
│     ├─ launch/
│     ├─ largemodel/
│     ├─ utils/
│     ├─ resources_file/
│     └─ MODELS/
├─ scripts/
├─ tests/
├─ docs/
└─ logs/
```

### 22.2 配置原则

- 模型路径写入YAML；
- 串口和声卡使用稳定设备别名；
- 不在代码中硬编码用户名；
- 不在仓库提交大模型文件；
- 不在仓库提交密钥；
- 每次交付记录JetPack、ROS、Python、模型和依赖版本；
- 配置变更需要保留变更记录。

### 22.3 模型版本记录

建议记录：

```yaml
asr:
  name: SenseVoiceSmall
  revision: "<模型版本或提交号>"

llm:
  name: qwen2.5:1.5b
  quantization: "4bit"
  context: 2048

tts:
  name: zh_CN-huayan-medium
  format: onnx
```

---

## 23. 推荐实施顺序

```text
1. 安装JetPack、Ubuntu 22.04和ROS 2 Humble
2. 配置NVMe和工作空间
3. 验证STM32串口与底盘安全保护
4. 验证麦克风和扬声器
5. 单独部署SenseVoiceSmall
6. 修复ASR短命令过滤和VAD静音帧问题
7. 单独部署Piper
8. 部署Qwen2.5 1.5B
9. 统一ROS话题为/asr
10. 完成ASR→LLM→TTS闭环
11. 加入播放期间ASR互斥
12. 加入上下文和输出长度限制
13. 联调导航与Function Call
14. 完成性能、稳定性和断网测试
15. 配置systemd
16. 最后评估3B模型和多模态扩展
```

---

## 24. 当前推荐发布基线

### 24.1 硬件

```text
Jetson Orin Nano 8GB
STM32F103
YDLidar
USB相机
USB麦克风
扬声器
双轮差速底盘
NVMe存储
```

### 24.2 软件

```text
Ubuntu 22.04
ROS 2 Humble
JetPack 6.x
Python 3.10
SenseVoiceSmall
Qwen2.5 1.5B 4bit
Piper中文medium
Ollama或llama.cpp
```

### 24.3 关键配置

```yaml
use_oline_asr: false
useolinetts: false
llm_platform: "ollama"
ollama_model: "qwen2.5:1.5b"
```

### 24.4 暂不纳入默认发布

```text
在线ASR
在线TTS
云端LLM
7B及以上模型
LLaVA
视频大模型
高并发Agent
不受限制的Shell工具
```

---

## 25. 结论

机器人系统采用Jetson Orin Nano 8GB与STM32分层架构：STM32保证底盘控制实时性和失联安全，Jetson负责ROS 2、导航、视觉和端侧AI。

离线语音链路采用SenseVoiceSmall、Qwen2.5 1.5B和Piper。系统以`/asr`作为ASR与模型服务之间的实际ROS 2话题，并由`model_service`直接调用本地TTS完成回复播放。

8GB平台的首要约束是共享内存。工程实现应优先采用小模型、短上下文、短回复和串行推理，在完成稳定性测试后再评估3B模型、多模态视觉模型和复杂Agent。
