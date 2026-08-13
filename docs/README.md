# Cockpit System 文档

常用文档全部平铺在本目录，只有历史参考材料放入 `reference/`。后续对话按任务读取最小集合，
不需要加载全部文档。

## 从哪里开始

| 任务 | 阅读文档 |
| --- | --- |
| 了解当前完成度和验证结果 | [status.md](status.md) |
| 继续当前开发 | [roadmap.md](roadmap.md) |
| 修改系统分层或模块 | [architecture.md](architecture.md)、[modularity.md](modularity.md) |
| 继续语音 Agent | [voice-agent.md](voice-agent.md)、[voice-agent-tasks.md](voice-agent-tasks.md) 顶部交接信息及当前阶段 |
| 修改 IPC 或进程通信 | [runtime-communication.md](runtime-communication.md) |
| 修改配置 | [configuration.md](configuration.md) |
| 构建、安装、诊断或回滚 | [deployment.md](deployment.md) |
| 修改代码结构和风格 | [code-style.md](code-style.md) |

## 文件清单

```text
docs/
├── README.md
├── status.md
├── roadmap.md
├── architecture.md
├── modularity.md
├── runtime-communication.md
├── voice-agent.md
├── voice-agent-tasks.md
├── configuration.md
├── deployment.md
├── code-style.md
├── workspace.md
├── changelog.md
└── reference/
    ├── zelos.md
    └── code-review-2026-07.md
```

`reference/` 是时点参考，不代表当前实现；`changelog.md` 只追加历史批次。当前事实只写入
`status.md`，下一步只写入 `roadmap.md`，稳定设计只写入架构文档，语音阶段勾选只写入
`voice-agent-tasks.md`。

## 维护规则

- 文档与代码冲突时，以当前代码和测试为准，并同步修正文档。
- 合并文档时先迁移独有内容，再删除重复段落；历史内容不因当前方案变化而改写。
- 每次对话只读取上表对应的最小文档集合。
- 提交前运行 `pre-commit run --all-files`。
