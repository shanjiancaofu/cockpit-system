# camera-service

Jetson 本地摄像头所有者。

```text
camera-service/
├── preview/  preview source 的 Runtime 生命周期包装
├── control/  设备校验、预览状态和健康指标
├── photo/    最新共享帧 JPEG 拍照
├── grpc/     list/start/stop/status/photo API
└── main.cc   共享内存 writer 和进程装配
```

GStreamer pipeline 采集的像素写入 POSIX shared memory 双缓冲，不经过 gRPC。状态接口包含接收帧、
拒绝帧、源端跳帧、最后序号和时间戳。reader 能检测 writer 退出并自动重连。

service 主循环通过 `CheckPreviewHealth()` 检查后端线程和最后收帧时间，将运行期故障分类为
`source_disconnected`、`no_frames` 或 `frame_stalled`。重新调用 start 可沿用现有恢复路径，并更新
restart/recover 指标。

拍照读取共享内存最新帧，通过 GStreamer `jpegenc` 写入受控目录，不重新打开摄像头。

WSL 合成稳定性验证：

```bash
bash scripts/run_camera_stability.sh 20
```
