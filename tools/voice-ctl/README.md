# voice-ctl

voice-interaction-service 的诊断客户端。

```bash
build/bin/voice-ctl --status --config configs/config.yaml
build/bin/voice-ctl --text "show vehicle status" --config configs/config.yaml
build/bin/voice-ctl --text "open camera" --config configs/config.yaml
build/bin/voice-ctl --watch --config configs/config.yaml
```

输入文本用于绕过真实 ASR，验证 intent、action、response 和 TTS 链路。
