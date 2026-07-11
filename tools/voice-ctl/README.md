# voice-ctl

voice-interaction-service 的诊断客户端。

```bash
build/bin/voice-ctl --status --config configs/config.yaml
build/bin/voice-ctl --process "show vehicle status" --config configs/config.yaml
build/bin/voice-ctl --responses --count 3 --config configs/config.yaml
build/bin/voice-ctl --status --output json --config configs/config.yaml
```

输入文本用于绕过真实 ASR，验证 intent、action、response 和 TTS 链路。
status/process/responses 均支持 `--output text|json`；responses 的 JSON 模式为一条事件一行。
