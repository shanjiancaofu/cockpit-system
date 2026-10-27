# camera-ctl

`camera_driver` 模块 CameraControl 接口的本地 gRPC 诊断客户端。

```bash
export COCKPIT_RUNTIME_DIR="$PWD/_output/runtime"
_output/build/x86_64-debug/bin/camera-ctl --list --config configs/development.yaml
_output/build/x86_64-debug/bin/camera-ctl --status --config configs/development.yaml
_output/build/x86_64-debug/bin/camera-ctl --status --output json \
  --config configs/development.yaml
_output/build/x86_64-debug/bin/camera-ctl --start --device /dev/video0 --width 640 --height 480 --fps 30 \
  --config configs/development.yaml
_output/build/x86_64-debug/bin/camera-ctl --stop --config configs/development.yaml
_output/build/x86_64-debug/bin/camera-ctl --photo --filename dashboard.jpg \
  --config configs/development.yaml
```

工具控制预览、查看状态并请求拍照，不通过 gRPC 传输视频帧。省略 `--filename` 时使用时间戳
命名；文件名只能包含字母、数字、`-`、`_`，并以 `.jpg` 结尾。

list/status/start/stop/photo 均支持 `--output text|json`。多个命令、非法 output 或非正数尺寸属于
参数错误，返回退出码 2。

Native CameraService 的同 Session Tuning 通过 `tuning-service` 调用，不会创建第二个 Argus
Session。先用 `tuning-service get` 取得当前 profile，再编辑后提交 set/apply；响应中的
`accepted` 表示 SDK 接受请求，`applied` 只有在对应 revision 出现在逐帧 Metadata 后才为 true。

```bash
_output/build/arm64-debug/bin/camera-ctl tuning-service get --address 127.0.0.1:50054 --output json
_output/build/arm64-debug/bin/camera-ctl tuning-service apply \
  --address 127.0.0.1:50054 --session-id 1 --revision 1 \
  --profile configs/camera/imx219_tuning.yaml --output json
```

## Native Hardware ISP 隔离控制

新增 `camera-ctl tuning get|set|reset|verify --native`，直接创建隔离 Native Session，
调用前停止占用相机的 Camera Service。该命令不会通过现有 gRPC 修改后台服务，设置仅对本次命令有效。

```bash
_output/build/arm64-debug/bin/camera-ctl tuning get --native --report _output/runtime/tuning.json
_output/build/arm64-debug/bin/camera-ctl tuning set --native --exposure-us 8000 --gain 2
_output/build/arm64-debug/bin/camera-ctl tuning set --native --ae auto --awb auto --ev 1
_output/build/arm64-debug/bin/camera-ctl tuning reset --native
_output/build/arm64-debug/bin/camera-ctl tuning --help
```

输出包含请求值、Request getter 读回值和逐帧 Metadata；新请求以 session_id/revision 标记。
`--report` 保存 JSON，`--frames`/`--warmup` 控制采集数量。
完整说明及验收见 [Camera V2 PR-05A](../../docs/CameraV2-PR05A.md)。

## Tuning Profile V1

```bash
mkdir -p _output/runtime/camera-pr05b
_output/build/arm64-debug/bin/camera-ctl tuning save --native \
  --profile _output/runtime/camera-pr05b/manual.yaml --profile-name manual \
  --exposure-us 8000 --gain 2 --wb-gains 1.5,1,1,1.25
_output/build/arm64-debug/bin/camera-ctl tuning load --native \
  --profile _output/runtime/camera-pr05b/manual.yaml --report _output/runtime/camera-pr05b/load.json
_output/build/arm64-debug/bin/camera-ctl tuning load --native \
  --profile configs/camera/imx219_tuning.yaml
```

save 保存 SDK Request 的读回值；load 核对实际 Sensor Model/Mode 和参数范围，并等待 Metadata 验证。
load 不接受单项覆盖，非默认格式需显式指定匹配的设备、尺寸和 FPS。
完整说明见 [PR-05B](../../docs/CameraV2-PR05B.md)。
