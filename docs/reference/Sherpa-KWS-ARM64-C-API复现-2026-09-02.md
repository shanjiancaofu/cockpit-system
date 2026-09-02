# Sherpa KWS ARM64 C API 复现记录

日期：2026-09-02。平台：Jetson Orin Nano 8GB，aarch64，Ubuntu 22.04 / JetPack 6。

## 结论

同一模型、关键词和WAV fixture下，Sherpa-ONNX官方内部C++ CLI可以检测“你好小山”，但公开C API
`SherpaOnnxKeywordSpotter`路径不产生关键词结果。该差异阻塞Cockpit V1真实麦克风唤醒门控，因此
Full-System 60min Gate保持BLOCKED。

## 固定输入

- Sherpa-ONNX：`v1.13.4`，commit `142807252687d81b40d6315f23470a1512a00de3`。
- ARM64 CPU shared archive SHA-256：
  `36c5a3c942358ed635471488f50a28a96181331c935b0dce75a02b7f49913dc2`。
- KWS模型：`sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20`，chunk-8 int8 encoder/joiner。
- Keyword：tokenized `你好小山`。
- Fixture：`_output/ai/fixtures/nihao-xiaoshan.wav`，16kHz mono PCM16，3.02秒。

## 一键复现

```bash
BUILD_DIR=_output/build/arm64-full-system-release \
bash scripts/tests/sherpa-kws-arm64-repro.sh
```

预期输出：

```text
official CLI: {"keyword": "你好小山", ...}
project C API: Sherpa KWS did not detect 你好小山
REPRODUCED
```

## 已排除

- v1.13.4预编译runtime：FAIL。
- v1.13.7 ARM64 CPU shared runtime：FAIL。
- v1.13.4同commit源码自建C API + ONNX Runtime 1.27.0：FAIL。
- file/buffer tokens与keywords：均FAIL。
- int8/FP32 KWS模型：均FAIL。
- `InputFinished`、0.3/0.5/0.8/2.0秒tail padding：均FAIL。
- C API struct与JSON读取：JSON keyword同样为空。
- `libsherpa-onnx-cxx-api.so`：底层仍调用公开C API，同样FAIL。

## Full-System影响

临时关闭KWS会使环境语音直接进入VAD/ASR/LLM/TTS。17分钟测试产生12次会话，Navigator进程树RSS
从约2.72GiB升至3.69GiB，系统可用内存降至约548MiB，swap使用约1.31GiB，因此测试按容量Gate提前
停止，不能作为生产替代方案。

## 后续选择

1. 向Sherpa-ONNX上游提交本复现。
2. 优先采用上游修复后的公开C API runtime。
3. 若上游短期无法修复，再在V2评估内部C++ KWS shim；不在V1临时引入私有ABI依赖。
