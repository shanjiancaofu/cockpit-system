# audio-probe

音频设备和 audio-service 诊断工具。

```bash
build/bin/audio-probe --list --config configs/config.yaml
build/bin/audio-probe --capture output.wav --seconds 3 --config configs/config.yaml
build/bin/audio-probe --play output.wav --config configs/config.yaml
build/bin/audio-probe --status --config configs/config.yaml
build/bin/audio-probe --status --output json --config configs/config.yaml
build/bin/audio-probe --start --config configs/config.yaml
build/bin/audio-probe --stop --config configs/config.yaml
build/bin/audio-probe --speak "hello" --config configs/config.yaml
```

设备命令直接诊断 ALSA；控制命令通过 gRPC 调用 audio-service。
start/stop/status/transcripts 支持 `--output text|json`，流式 transcript 使用 JSON Lines。本地 ALSA
list/capture/play 和有副作用的 speak 保持人类可读输出，传入 JSON 模式会返回参数错误。
