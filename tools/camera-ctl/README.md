# camera-ctl

camera-service 的本地 gRPC 诊断客户端。

```bash
build/bin/camera-ctl --list --config configs/config.yaml
build/bin/camera-ctl --status --config configs/config.yaml
build/bin/camera-ctl --status --output json --config configs/config.yaml
build/bin/camera-ctl --start --device /dev/video0 --width 640 --height 480 --fps 30 \
  --config configs/config.yaml
build/bin/camera-ctl --stop --config configs/config.yaml
build/bin/camera-ctl --photo --filename dashboard.jpg --config configs/config.yaml
```

工具控制预览、查看状态并请求拍照，不通过 gRPC 传输视频帧。省略 `--filename` 时使用时间戳
命名；文件名只能包含字母、数字、`-`、`_`，并以 `.jpg` 结尾。

list/status/start/stop/photo 均支持 `--output text|json`。多个命令、非法 output 或非正数尺寸属于
参数错误，返回退出码 2。
