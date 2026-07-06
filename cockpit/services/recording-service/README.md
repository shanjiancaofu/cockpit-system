# recording-service

面向研发诊断的数据录包进程，不属于用户语音交互。

第一阶段订阅 `vehicle-data-service`，按会话写入：

```text
data/recordings/sessions/<session-id>/
├── manifest.json
├── vehicle_state.jsonl
└── COMPLETE
```

控制面提供 start、stop、status。相机、音频、事件和 SQLite 索引在后续阶段接入。
