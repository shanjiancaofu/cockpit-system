# 本地工作区

WSL 和 Jetson 都按源码、外部资源和生成物分开。用户名及工作区根目录由机器决定，不写入脚本和
配置合同：

```text
<workspace>/
├── github/
│   ├── cockpit-system/              主项目
│   └── chassis-controller/          STM32 下位机项目（存在时）
├── models/                           独立模型资源，不进入 Git
└── third_party/                      外部依赖源码或缓存，不进入主仓库
```

## 放置规则

- 项目源码和受 Git 管理的文档只放在 `github/cockpit-system`；WSL 当前示例是
  `/home/ffz/code/github/cockpit-system`，Jetson 可使用 `/home/nvidia/code/github/cockpit-system`。
- 项目构建、测试、实验和运行产物统一放在
  `github/cockpit-system/_output`。
- 本地生成的 WAV 放在 `_output/runtime/audio`，运行日志放在
  `_output/runtime/logs`。
- Sherpa-ONNX、ONNX Runtime、llama.cpp 和模型按固定版本放在外部资源目录或受控依赖缓存，
  不提交到主仓库，也不在程序启动时下载。
