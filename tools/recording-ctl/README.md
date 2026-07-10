# recording-ctl

recording-service 的本地控制工具。

```bash
build/x86_64-debug/bin/recording-ctl --status --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --start --trigger manual --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --stop --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --list --limit 20 --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --detail <session-id> --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --delete <session-id> --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --prune --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --event-topic /dev/event --event-payload '{"ok":true}' --config configs/config.yaml
build/x86_64-debug/bin/recording-ctl --file-path photos/frame.jpg --file-source camera --file-kind jpeg --file-size-bytes 1024 --file-checksum sha256:demo --config configs/config.yaml
```
