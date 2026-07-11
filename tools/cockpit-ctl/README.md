# cockpit-ctl

聚合查看车端服务状态。

```bash
build/bin/cockpit-ctl status --config configs/config.yaml
build/bin/cockpit-ctl status --watch --interval 2 --config configs/config.yaml
build/bin/cockpit-ctl health --config configs/config.yaml
build/bin/cockpit-ctl status --output json --config configs/config.yaml
build/bin/cockpit-ctl health --output json --config configs/config.yaml
```

当前汇总 gateway、audio、voice、camera 和 recording。部分服务离线时会立即报告 unavailable，
不等待服务上线。相机状态包含 frame health、帧龄、最后序号和跳帧数。

`--output json` 输出单个聚合 JSON；status 会为不可达服务保留 `available: false` 和错误原因。
`health` 面向脚本、systemd 和部署检查：通过返回 0，任一服务状态未知或 faulted 返回 3。
