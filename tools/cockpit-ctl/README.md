# cockpit-ctl

聚合查看车端服务状态。

```bash
build/bin/cockpit-ctl status --config configs/config.yaml
build/bin/cockpit-ctl status --watch --interval 2 --config configs/config.yaml
```

当前汇总 gateway、audio、voice 和 camera。部分服务离线时会立即报告 unavailable，不等待服务上线。
相机状态包含 frame health、帧龄、最后序号和跳帧数。
