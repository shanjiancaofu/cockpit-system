# 文档导航 / Documentation

本目录是 cockpit-system 的文档入口。当前实现以
`/home/ffz/code/github/cockpit-system` 仓库为准。

## 文档层级

1. [architecture_refined_v0.3.md](architecture_refined_v0.3.md)
   - 完整系统蓝图。
   - 描述长期模块、通信方式、部署和工程规范。
   - 不代表所有模块都要在当前阶段实现。
2. [architecture.md](architecture.md)
   - 当前代码对应的架构快照。
   - 记录已经落地的链路和最近开发顺序。
3. [project_scope_and_repo_strategy.md](project_scope_and_repo_strategy.md)
   - 当前项目范围和是否拆分仓库的决策。
4. [modularization_strategy.md](modularization_strategy.md)
   - 一个主仓库、多个内部模块以及未来拆库条件。
5. [implementation_status.md](implementation_status.md)
   - 已完成、未完成和验证状态。
6. [change_log.md](change_log.md)
   - 每批改动的中英双语记录。

## 参考与专项

- [reference_projects.md](reference_projects.md)：旧项目与开源项目参考。
- [reference_code_audit.md](reference_code_audit.md)：旧代码细粒度审计。
- [voice_ai_plan.md](voice_ai_plan.md)：麦克风、扬声器、语音与大模型规划。

## 当前仓库策略

```text
当前：cockpit-system 一个主仓库
内部：core / can / 后续 audio / ai / proto
未来：出现独立部署和发布边界后，再考虑 cloud-backend / cloud-frontend
```

目前不创建云端前后端仓库。Jetson 本机车辆链路、UI、调试工具和服务接口稳定后，
再根据实际需求决定是否拆分。

## 维护规则

- 代码行为变化后更新 `implementation_status.md`。
- 架构边界变化后更新 `architecture.md`。
- 每批修改都追加到 `change_log.md`。
- 总体蓝图与当前实现冲突时，以 scope、implementation status 和当前代码为准，并回头修订蓝图。
