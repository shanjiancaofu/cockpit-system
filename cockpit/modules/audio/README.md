# audio 模块

```text
audio/
├── frames/    PCM 格式、AudioFrame、SPSC queue
├── capture/   采集接口和采集线程
├── vad/       VAD 和语音分段
├── playback/  平台无关播放接口
└── wav/       RIFF/WAVE 读写
```

对应 target：`audio_frames`、`audio_capture`、`audio_vad`、`audio_playback`、`audio_wav`。
父级 `audio` 仅用于兼容聚合，新代码应链接最小 target。

音频数据固定为 PCM16、16 kHz、mono、20 ms frame。采集线程是 SPSC producer，VAD/ASR
pipeline 是 consumer；队列满时丢帧并记录指标，不阻塞采集。

本模块不直接打开麦克风或扬声器，ALSA 实现在 `cockpit/drivers/alsa`，进程生命周期在
`cockpit/services/audio-service`。
