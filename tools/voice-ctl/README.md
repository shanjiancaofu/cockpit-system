# voice-ctl

`agent` 模块 VoiceInteraction 接口的诊断客户端。

```bash
export COCKPIT_RUNTIME_DIR="$PWD/_output/runtime"
_output/build/x86_64-debug/bin/voice-ctl --status --config configs/development.yaml
_output/build/x86_64-debug/bin/voice-ctl --process "show vehicle status" \
  --config configs/development.yaml
_output/build/x86_64-debug/bin/voice-ctl --responses --count 3 \
  --config configs/development.yaml
_output/build/x86_64-debug/bin/voice-ctl --status --output json \
  --config configs/development.yaml
```

输入文本用于绕过真实 ASR，验证 intent、action、response 和 TTS 链路。
status/process/responses 均支持 `--output text|json`；responses 的 JSON 模式为一条事件一行。
