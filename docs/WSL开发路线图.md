# WSL 开发路线图

更新时间：2026-07-12。

本文只规划不依赖 Jetson、正式 DBC、真实麦克风/扬声器和 CSI 摄像头的工作。优先级按“提高单机
可运行、可测试、可诊断能力”排序；每批完成后再进入下一批，不并行铺开新的 service 或框架。

## 审计结论

当前源码约 2.3 万行 C++/proto，默认构建包含 37 项 CTest。车辆、音频、语音、相机、录包、UI 和
诊断控制面已有 WSL 闭环，后续不再补工程骨架。参考 znavigator 的配置验证、生命周期和运行证据，
参考 zcarcloud 的 transport/业务边界与可测试入口，但不引入动态插件、通用 manager 或云端规模。

## WSL 批次

在 W2 前先完成结构收口批次：保持功能冻结，整理大入口、直接依赖和重复状态文档。该批次不改变
运行架构，具体范围见 [结构收口清单.md](结构收口清单.md)。

### W1 健康变化历史

状态：已完成。

- UI 进程保留最近 32 条服务状态切换，初始采样不记为变化。
- 每个服务保留最近一次 degraded/faulted 的时间和原因，恢复后仍可追溯。
- Dashboard 展示最近问题，Diagnostics 页面展示有界变化列表。
- 测试覆盖问题、恢复、原因选择和容量淘汰。

### W2 topic 元数据与错误诊断

状态：下一批。

- TopicMetadata 增加 transport、期望更新周期和当前可用性说明。
- `topic info/list` 输出不可订阅原因；NOT_FOUND、UNAVAILABLE、DEADLINE_EXCEEDED 使用稳定分类。
- 测试覆盖未知 topic、服务离线、不可订阅 topic 和 JSON/text 输出。

完成条件：不用查看 gateway 日志即可判断 topic 来自哪里、多久更新、为什么不能订阅。

### W3 可重复长稳测试与报告

状态：待实现。

- 组合 mock VehicleState、null audio、synthetic camera、voice 和 recording 运行固定时长。
- 周期采样 health、关键计数和进程退出状态，结束后执行 `recording-ctl --verify-all`。
- 生成单个 JSON 报告，记录版本、配置 checksum、持续时间、故障注入和失败阶段。
- 提供短时 CI 模式和开发机长时模式，不把偶发 deadline 静默重试成成功。

完成条件：一次命令可复现 10 分钟到数小时的 WSL 稳定性运行，并留下机器可读证据。

### W4 录包离线诊断报告

状态：待实现。

- 基于现有 list/detail/timeline/verify 接口导出会话摘要，不增加数据库。
- 报告包含 manifest 元数据、完整性、损坏行、时间范围和 artifact 列表。
- 批量报告限制会话数和 issue 数；原始 JSONL 始终是权威来源。

完成条件：长稳测试产生的录包无需手工拼接多个 CLI 输出即可交给研发分析。

### W5 配置、打包和故障矩阵

状态：待实现。

- 扩充配置字段路径、期望类型和范围错误，保证启动前失败可定位。
- 在临时目录验证 package、install、healthcheck 和 rollback，不伪装成 Jetson systemd 验证。
- 为 gateway 断流、recording 不可达、audio 队列满和 shared memory writer 重启补可重复故障场景。

完成条件：常见配置错误和弱依赖故障均有自动测试，release 包可在 WSL 临时根目录验收。

### W6 语音与云端的无硬件部分

状态：低优先级。

- 用 mock transcript/TTS 验证打断、连续命令、超时和 provider 失败，不宣称完成真实声学体验。
- cloud-uplink 先定义可测试的连接状态、重试和有界发送语义；只有本地 broker 依赖明确后再接 MQTT。
- LLM provider 先等产品交互和网络边界明确，不为了占位增加接口。

完成条件：状态机和错误语义可测；真实模型、TLS 和公网兼容性仍标记为未验证。

## 硬件到位后

以下工作不在 WSL 阶段宣称完成：正式 DBC、真实 CAN 总线、麦克风/扬声器/AEC、CSI/libargus、
Jetson GStreamer pipeline、CUDA/TensorRT 性能、systemd 权限和长运行、真实 MQTT/TLS 网络以及设备
时钟偏移/漂移标定。WSL 只准备接口、仿真和失败路径，最终参数必须由实机数据决定。
