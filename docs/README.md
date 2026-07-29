# 文档导航

本目录是 cockpit-system 的文档入口。当前实现以
`/home/nvidia/code/github/cockpit-system` 仓库为准。

## 权威文档

| 职责 | 文档 | 只回答什么 |
|---|---|---|
| 当前架构 | [architecture.md](architecture.md) | 进程、模块、依赖和通信边界 |
| 实现状态 | [实现状态.md](实现状态.md) | 已实现内容、入口和验证方式 |
| 当前计划 | [项目进度总览.md](项目进度总览.md) | 下一步、风险和硬件阻塞项 |
| 工程规则 | [模块化策略.md](模块化策略.md)、[代码风格.md](代码风格.md) | 目录、target、抽象和编码约束 |
| 本地工作区 | [本地工作区结构.md](本地工作区结构.md) | 源码、模型、第三方依赖和运行产物放置规则 |
| 运行说明 | [配置说明.md](配置说明.md)、[部署说明.md](部署说明.md) | 配置、构建、打包、安装和回滚 |
| 通信专题 | [运行时通信策略.md](运行时通信策略.md) | 控制面、数据面和时间语义 |
| 语音专题 | [语音AI规划.md](语音AI规划.md) | ASR、TTS、LLM 和动作安全边界 |
| 参考材料 | [参考项目.md](参考项目.md) | 旧项目可复用结论，不作为当前设计依据 |
| 历史记录 | [变更记录.md](变更记录.md) | 已完成批次及验证结果 |
| 静态审核 | [cockpit-system-code-review.md](cockpit-system-code-review.md) | 2026-07 全仓库问题清单和修复依据 |

## 目录放置速查

```text
_output/             WSL 构建、打包、日志、数据和报告（不入库）
cockpit/             产品源码根目录
  core/              配置、日志、生命周期和 IPC
  navigator/         统一入口、运行模式和进程管理
  library/           进程级动态业务模块
  modules/           vehicle、can、audio、camera、voice
  drivers/           SocketCAN、ALSA、V4L2
  apps/              Qt/QML 车机应用
  proto/             服务间接口契约
tools/               调试工具、仿真器和 probe
configs/             开发与生产应用配置
cmake/               工程选项、依赖、安装规则和 Jetson 工具链
deploy/              随发布包交付的安装、回滚、systemd 和许可证资产
scripts/             工程自动化命令；tests/ 统一保存 Shell 测试入口
tests/               按领域组织的 C++ 测试源码和测试夹具
```

## 当前仓库策略

```text
当前：cockpit-system 一个主仓库
内部：core 基础设施 / modules 领域能力 / drivers 硬件适配 / proto 接口契约
未来：出现独立部署和发布边界后，再考虑 cloud-backend / cloud-frontend
```

目前不创建云端前后端仓库。Jetson 本机车辆链路、UI、调试工具和模块合同稳定后，
再根据实际需求决定是否拆分。

## 维护规则

- 代码行为变化后更新 `实现状态.md`。
- 架构边界变化后更新 `architecture.md`。
- 每批修改都追加到 `变更记录.md`。
- 文档与代码冲突时，以当前代码和测试为准，并立即修订对应职责文档。
- 提交前运行 `pre-commit run`，或执行 `pre-commit install` 启用 Git hook。
- 静态检查手动运行 `pre-commit run clang-tidy --hook-stage manual -a`，不作为普通 commit 强制门禁。
