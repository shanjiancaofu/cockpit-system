# audio-probe

音频设备和 `audio_driver` 模块诊断工具。

```bash
export COCKPIT_RUNTIME_DIR="$PWD/_output/runtime"
_output/build/x86_64-debug/bin/audio-probe --list --config configs/config.yaml
_output/build/x86_64-debug/bin/audio-probe --capture output.wav --seconds 3 --config configs/config.yaml
_output/build/x86_64-debug/bin/audio-probe --play output.wav --config configs/config.yaml
_output/build/x86_64-debug/bin/audio-probe --status --config configs/config.yaml
_output/build/x86_64-debug/bin/audio-probe --status --output json --config configs/config.yaml
_output/build/x86_64-debug/bin/audio-probe --start --config configs/config.yaml
_output/build/x86_64-debug/bin/audio-probe --stop --config configs/config.yaml
_output/build/x86_64-debug/bin/audio-probe --speak "hello" --config configs/config.yaml
```

设备命令直接诊断 ALSA；控制命令通过 gRPC 调用 `audio_driver` 提供的 AudioControl 接口。
start/stop/status/transcripts 支持 `--output text|json`，流式 transcript 使用 JSON Lines。本地 ALSA
list/capture/play 和有副作用的 speak 保持人类可读输出，传入 JSON 模式会返回参数错误。
