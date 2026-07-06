# recording-service

面向研发诊断的数据录包进程，不属于用户语音交互。

第一阶段订阅 `vehicle-data-service`，按会话写入：

```text
data/recordings/sessions/<session-id>/
├── manifest.json
├── vehicle_state.jsonl
└── COMPLETE
```

控制面提供 start、stop、status、list、delete 和 prune。服务启动时恢复异常会话并重建索引，
完成录制后自动执行会话数与总空间保留策略。

当前完成的是 VehicleState 结构化研发录包。相机 MP4、音频 WAV 和跨源时间对齐需要独立的
数据源 writer 与编码策略，不属于本阶段已经完成的能力。
