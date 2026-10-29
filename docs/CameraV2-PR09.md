# Camera V2 PR-09：事件视频录像

PR-09 的视频侧基础闭环位于 `cockpit/spider/recording/video/`：

- `H264GopRing` 只保留从关键帧开始的完整 GOP，默认保留事前 10 秒。
- `EventVideoRecorder` 触发事件后写入环形缓存，并继续写入事后 20 秒。
- `Mp4Muxer` 通过 GStreamer `h264parse ! mp4mux` 生成可播放 MP4。

录制器接收已经编码的 `EncodedVideoPacket`，不会创建第二个编码器。当前已完成库级闭环和
GOP 单元测试；相机编码帧到 recording 进程的桥接、事件源联动和 Jetson MP4 播放验收是
下一步接入项。

验证：

```bash
cmake --build _output/build/arm64-debug-auto -j2
ctest --test-dir _output/build/arm64-debug-auto \
  -R 'h264_gop_ring|recording_(session|catalog|timeline|integrity|service)_test' \
  --output-on-failure
```
