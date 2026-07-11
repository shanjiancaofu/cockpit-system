# recording-ctl

recording-service 的本地控制工具。

```bash
build/x86_64-debug/bin/recording-ctl --status --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --start --trigger manual --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --stop --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --list --limit 20 --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --detail <session-id> --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --timeline <session-id> --from-ms 0 --to-ms 0 --limit 100 --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --delete <session-id> --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --prune --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --event-topic /dev/event --event-payload '{"ok":true}' --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --file-path photos/frame.jpg --file-source camera --file-kind jpeg --copy-into-session --config configs/config.yaml
```

`--copy-into-session` 将源文件复制到当前会话的 `artifacts/`，使 retention 统计和清理覆盖文件本体；
不带该参数时只记录外部路径索引。未启动录制时，追加事件或文件命令返回非零。

`--timeline` 将 VehicleState、event 和 data file 按主机 `timestamp_ms` 稳定归并；`--to-ms 0`
表示不限制结束时间，查询最多返回 1000 条。
