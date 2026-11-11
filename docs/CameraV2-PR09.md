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

## 2026-11-11：Recording Correctness / GOP Memory Hardening

本节更新早期库级说明。Camera→Recording 桥接及此前的实机结果见
`CameraV2-PR10-acceptance.md`；本轮针对当前录像实现修复正确性和资源上限，不修改
Native Camera、BSP、RTSP/WebRTC，也不增加生产编码器或连续录像功能。

### MP4 结束与失败状态

- `Mp4Muxer::Close()` 只有在 EOS 正常提交、5 秒内收到 EOS、没有先前 Write 失败或
  Pipeline ERROR 且 Pipeline 停止成功时才返回 true。超时不再误报成功，返回值与调用者
  是否传入 `error`、字符串是否含旧错误无关。
- Write/Close 错误保持到下一次 Open；后续析构、Finish、Poll 不会把失败重置成成功。
- GStreamer Pipeline 使用固定拓扑，输出文件路径通过属性设置，不拼入 launch 表达式。
- 事件先写 `<name>.mp4.incomplete`。EOS 成功后检查 MP4 顶层 ftyp/moov/非空 mdat 与 box
  边界，再用同目录 hard link 原子且不覆盖地发布 `<name>.mp4`，最后移除临时名字。
  这是结构检查，不是每次录像都额外执行一次全片解码。文件系统不支持 hard link 时明确
  失败，不退回到可能覆盖已有事件的发布方式。
- 失败保留 `.incomplete` 便于诊断，不作为 `video/mp4` 产物登记；没有新增无限重试或
  独立存储平台。沿用 Session 的目录同步/COMPLETE/保留策略。该发布方式不宣称提供
  新的断电事务保证，异常掉电仍按既有 Session 完整性规则处理。
- Service 遇到视频写入或收尾失败时记录 `/recording/video/failed`（尽力写入），保留
  `last_error` 并退休 pending video，不能在后续 Stop/Poll 注册为成功产物。Stop 中首次
  发现视频收尾错误会返回 false；错误视频退休后可再次 Stop 完成 Session 清理。

### GOP 缓存边界

- `Append/Trim` 不再调用 `Snapshot`，最新 PTS 直接取本次追加包；packet 数和编码字节数
  增量维护，新增 payload 拷贝前先淘汰超预算旧 GOP。HistoryStartPts 也不再复制整个窗口，
  Snapshot 仅在事件触发时拷贝历史数据。
- 默认编码数据预算 64 MiB，可通过 `EventVideoRecorderConfig::max_history_bytes` 设置。
  此值是 H264 payload 预算，不包含容器/allocator 元数据，也不是整个进程的 RSS 上限。
- 旧数据按完整 GOP 淘汰，保留的起点仍是 IDR。单 GOP 超出预算或超过历史时长仍无新 IDR
  时，清空历史并等待下一 IDR；记录 dropped GOP 计数并在 RecordingService 状态中提示。
  本轮不新增 Camera 控制通路，沿用现有周期 IDR 恢复。
- 历史缓存淘汰不影响已经打开的事件录像：直播包仍独立写入 muxer，不因 Ring 返回 false
  就跳过活跃录像的参考帧。

### 自动验证与边界

```bash
cmake --build _output/build/arm64-debug-auto --target \
  mp4_failure_test h264_gop_ring_test recording_service_test \
  recording_session_test recording_catalog_test recording_timeline_test recording_integrity_test -j2

ctest --test-dir _output/build/arm64-debug-auto --output-on-failure \
  -R '^(mp4_failure_test|h264_gop_ring_test|recording_.*test)$'
```

`mp4_failure_test` 使用真实 CPU H264 fixture、GStreamer MP4 封装/解码，并用测试可执行文件
专属链接包装注入 EOS timeout、Pipeline ERROR、EOS 提交失败与 Write 失败。
超时注入模拟 5 秒等待返回空消息，不实际耗时等待；生产仍使用 5 秒截止时间。
覆盖有/无 error 指针、重复 Close/Finish、预录写入失败、后续事件恢复、临时文件发布、
结构检查失败、发布重名保护，以及 Service 后续 Poll/Stop 不得补登记失败 MP4。
缺少所需 GStreamer 插件时 CTest 明确标记 Skipped，不计为通过。

GOP 测试覆盖完整 GOP 淘汰、单包超预算、无 IDR 时长上限、下一 IDR 恢复和 10,000 包的
计数/预算/解码起点一致性；另以 ASan/UBSan 执行该测试。
这些测试不占用 Sensor，不需要 sudo/reboot，不代替 Camera 全链路真机回归、真实磁盘满/
掉电测试或标准 VLC 人工观看。RTSP push 返回值、消费者异常统计及 SIGKILL 定位另行处理。


### 2026-11-11 真机补充：生成产物索引修复

真实连续录像/Camera 重启回归中，三段 MP4 均可解码，但首次 `--verify-all` 报告损坏。
原因是 `artifacts/event_video_N.mp4` 已在 Session 内生成，索引却仍使用外部路径语义。
失败证据保留在 `_output/runtime/media-recovery-20261111/normal-chrome/`，没有改写为通过。

新增仅供本地可信 producer 使用的 `RecordingSession::AppendGeneratedDataFile`：
验证路径位于当前 Session 的 artifacts/、拒绝非规范路径/逃逸/符号链接、核验大小，
同步文件及 artifacts 目录，将 payload 纳入既有 Session 容量计数，以既有索引字段
`copied_into_session=true` 标记相对存储位置，但不再实际复制一遍视频。外部导入仍走原有
AppendDataFile/CopyDataFile，不新增 RPC 或存储框架。

修复后的最终真机结果：连续两次事件及 Camera 重启后的第三次事件全部完成；录制期间
均观察到 `.mp4.incomplete`，最终无残留；三段均发现/解码到 EOS。Chrome 原生播放器
实测第三段 1280×720、9.133 秒，播放 0.0515→2.0661 秒、显示帧 9→71，无 video error。
`recording-ctl --verify-all`：1 个健康 Session、3 个文件、0 个 issue。

单元测试补充生成路径拒绝、大小不匹配拒绝以及 Session 目录重命名后 MP4 checksum 校验。
最终真机证据位于 `_output/runtime/media-recovery-20261111/final-normal/`，不入 git。
