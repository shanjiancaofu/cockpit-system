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

拍照读取共享内存最新帧，通过 GStreamer `jpegenc` 写入受控目录，不重新打开摄像头。
