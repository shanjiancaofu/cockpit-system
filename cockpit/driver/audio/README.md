# audio 模块

```text
audio/
├── frames/    PCM 格式、AudioFrame、SPSC queue
├── capture/   采集接口和采集线程
├── transport/ 驱动进程与 Agent 之间的 PCM 线协议
├── playback/  平台无关播放接口
└── wav/       RIFF/WAVE 读写
```

对应基础 target：`audio_frames`、`audio_capture`、`audio_stream_protocol`、
`audio_playback`、`audio_wav`。父级 `audio` 仅用于兼容聚合，新代码应链接最小 target。

采集音频固定为 PCM16、16 kHz、mono、20 ms frame。采集线程是 SPSC producer，音频流发布线程是
consumer；队列满时丢帧并记录指标，不阻塞采集。播放使用独立的 `PcmBuffer` 合同，可承载 16 kHz
mono 固定提示音和 24 kHz mono Kokoro 输出；具体物理采样率和声道转换由配置的 ALSA device 负责。

本模块不直接打开麦克风或扬声器，ALSA 实现在 `cockpit/drivers/alsa`，进程级装配和生命周期在
`cockpit/library/driver/audio`。

VAD、语音分段、ASR 和 TTS 位于顶层 `agent/speech`，不属于 audio 基础模块。
