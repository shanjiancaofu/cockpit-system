# cockpit-ctl

聚合查看车端模块接口状态，并控制 Navigator Runtime。

```bash
export COCKPIT_RUNTIME_DIR="$PWD/_output/runtime"
_output/build/x86_64-debug/bin/cockpit-ctl status --config configs/config.yaml
_output/build/x86_64-debug/bin/cockpit-ctl status --watch --interval 2 --config configs/config.yaml
_output/build/x86_64-debug/bin/cockpit-ctl health --config configs/config.yaml
_output/build/x86_64-debug/bin/cockpit-ctl status --output json --config configs/config.yaml
_output/build/x86_64-debug/bin/cockpit-ctl health --output json --config configs/config.yaml
_output/build/x86_64-debug/bin/cockpit-ctl runtime status --socket /tmp/cockpit-navigator.sock
_output/build/x86_64-debug/bin/cockpit-ctl runtime restart camera_driver --socket /tmp/cockpit-navigator.sock
_output/build/x86_64-debug/bin/cockpit-ctl snapshot \
  --socket /tmp/cockpit-navigator.sock --config configs/config.yaml \
  --max-snapshots 10 --max-total-bytes 104857600
```

当前汇总 transfer、audio、agent、camera 和 recording 暴露的 gRPC 接口。部分模块离线时会立即报告
unavailable，不等待模块上线。相机状态包含 frame health、帧龄、最后序号和跳帧数。

`--output json` 输出单个聚合 JSON；status 会为不可达服务保留 `available: false` 和错误原因。
`health` 面向脚本、systemd 和部署检查：通过返回 0，任一接口状态未知或 faulted 返回 3。
`runtime` 通过本地 Unix Socket 查询、切换或重启 Navigator 模块；连接和请求最多等待 1 秒。
`snapshot` 收集配置摘要、Navigator 状态、聚合状态和有界日志。默认最多保留 10 份、合计
100 MiB，超过任一上限后删除同目录中最旧的 `snapshot-*`；稳定性脚本在首次失败时自动调用一次。
