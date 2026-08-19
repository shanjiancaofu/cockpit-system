# topic

参考 ROS topic 使用方式的本地调试工具。

## 命令

```bash
topic list
topic info /vehicle/state
topic echo /vehicle/state
topic hz /vehicle/state
topic pub /dev/test '{"ok":true}'
```

`/vehicle/state` 使用 live gRPC backend：

- `list`：发现 gateway 可订阅 topic。
- `info`：显示类型、来源、transport、预期更新周期、可用状态、最近更新时间和错误原因。
- `echo`：输出实时消息。
- `hz`：计算接收频率。

gRPC topic 的可用状态是稳定枚举：`available`、`waiting_for_data`、`stale`。自动化应判断该字段，
`error reason` 只用于补充现场信息。

开发 topic 可以使用 file backend。开发 YAML 默认目录是
`_output/runtime/logs/topics`；设置 `COCKPIT_RUNTIME_DIR` 后会覆盖到该运行目录下的
`logs/topics`。file backend 只用于单机调试，不是正式 Runtime MessageBus。

## 文件组织

每个命令独立实现：

```text
topic_list.cc
topic_info.cc
topic_echo.cc
topic_hz.cc
topic_pub.cc
```

保持这种按命令命名的扁平结构，不再增加模糊的 command/common 子目录。
