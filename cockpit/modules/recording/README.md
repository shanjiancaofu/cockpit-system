# recording 模块

提供与 transport 无关的录包会话文件管理：

- 创建 `.recording_<id>` 临时目录。
- 写入 `manifest.json`、`vehicle_state.jsonl` 和 `events.jsonl`。
- 停止时原子改名并创建 `COMPLETE` 标记。
- 启动时识别上次异常中断的临时目录。
- 扫描 manifest 建立内存目录索引，按时间倒序查询历史会话。
- 删除受管会话，按最大会话数和总字节数清理最旧数据。
- 将损坏 manifest 标记为 `corrupted`，避免单个坏会话阻止服务启动。

本模块不订阅 gRPC、不读取摄像头或音频设备；数据订阅和进程生命周期属于
`cockpit/processes/recording`。

`vehicle_state.jsonl` 保存强类型车况数据；`events.jsonl` 保存轻量研发事件，例如相机帧元数据、
拍照结果、语音识别结果等。大图像、音频和视频文件不直接塞进事件流，后续应通过独立文件或共享内存
句柄记录，再由事件保存路径、句柄和时间戳。

原始文件是权威数据，目录索引可在启动时重建。当前不需要 SQLite；未来数据规模和查询条件
明显增加时，可以在不改变会话目录格式的前提下替换索引后端。
