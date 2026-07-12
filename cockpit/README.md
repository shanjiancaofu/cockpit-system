# cockpit 产品源码

该目录包含最终车机产品的代码：

```text
cockpit/
├── cockpit/apps/       Qt/QML 用户应用
├── cockpit/core/       配置、日志、Runtime、事件和 IPC
├── cockpit/drivers/    ALSA、SocketCAN、V4L2 等 Linux 适配
├── cockpit/modules/    audio、camera、vehicle、voice 等领域能力
├── cockpit/proto/      protobuf/gRPC 契约
└── cockpit/processes/   车端长运行进程
```

仓库根目录的 `tools/`、`tests/`、`docs/`、`cmake/`、`configs/` 和 `scripts/` 属于开发、测试、
构建或部署支持，不进入产品源码目录。

`cockpit/CMakeLists.txt` 负责聚合产品 target；仓库顶层 CMake 再聚合工具、测试和安装规则。
