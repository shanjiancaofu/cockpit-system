# recording-ctl

recording-service 的本地控制工具。

```bash
build/x86_64-debug/bin/recording-ctl --status --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --start --trigger manual --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --stop --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --list --limit 20 --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --detail <session-id> --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --timeline <session-id> --from-ms 0 --to-ms 0 --limit 100 --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --verify <session-id> --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --verify-all --from-started-ms 0 --to-started-ms 0 --limit 100 --output json --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --delete <session-id> --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --prune --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --event-topic /dev/event --event-payload '{"ok":true}' --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --file-path photos/frame.jpg --file-source camera --file-kind jpeg --copy-into-session --config configs/config.yaml
```

`--copy-into-session` 将源文件复制到当前会话的 `artifacts/`，使 retention 统计和清理覆盖文件本体；
不带该参数时只记录外部路径索引。未启动录制时，追加事件或文件命令返回非零。

`--timeline` 将 VehicleState、event 和 data file 按主机 `timestamp_ms` 稳定归并；`--to-ms 0`
表示不限制结束时间，查询最多返回 1000 条。

`--verify` 校验 data file 索引、路径、文件类型、大小和受支持的 checksum。完整性问题会逐项输出并
返回退出码 2；当前支持 `fnv1a64` checksum，空值和其他算法计入 unavailable，不作为文件损坏。

`--verify-all` 按会话开始时间批量汇总 healthy、damaged 和 unavailable，单个 manifest 不可读不会
中断整批。`--to-started-ms 0` 表示不限制结束时间，最多检查 1000 个会话；`total_sessions` 是过滤后
的总数，`sessions` 和各状态计数只覆盖本次 limit 内实际检查的会话。支持 text/JSON 输出，存在
damaged 或 unavailable 时返回退出码 3，再使用 `--verify <session-id>` 查看损坏明细。
