# Sherpa KWS 模型兼容性决策（2026-09-02）

Cockpit V1 固定以下组合：

```text
sherpa-onnx runtime: v1.13.4 ARM64 public C API
KWS model: sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01-mobile
variant: epoch-12 chunk-16, int8 encoder/joiner
keyword: n ǐ h ǎo x iǎo sh ān @你好小山
```

同一Jetson、同一runtime和同一Cockpit provider验证结果：

```text
你好小山         detect
你好小车         no detect
silence          no detect
command-only     no detect
Voice Gate       action path PASS
```

`sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20`的chunk-8模型在官方内部C++ CLI可检测，
但公开C API路径在本机测试中不返回关键词。该现象不代表Sherpa C API整体损坏，因为2024
WenetSpeech mobile模型在相同C API路径正常工作。

因此V1采用模型回退和版本固定，不修改Sherpa runtime，不更换SenseVoice ASR，也不在当前
阶段提交上游Issue。新模型的一键复现和原始判断保留在
[`Sherpa-KWS-ARM64-C-API复现-2026-09-02.md`](Sherpa-KWS-ARM64-C-API复现-2026-09-02.md)。

旧模型不包含`en.phone`。模型准备脚本只提供已经验证的默认tokenized关键词；自定义关键词
必须提供与该模型`tokens.txt`兼容的tokenized文件，不能把原始中文直接传给C API。
