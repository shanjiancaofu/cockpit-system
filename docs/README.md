# 文档导航

本目录是 cockpit-system 的文档入口。当前实现以
`/home/ffz/code/github/cockpit-system` 仓库为准。

## 文档层级

1. [architecture_refined_v0.4.md](architecture_refined_v0.4.md)
   - 完整系统蓝图。
   - 描述长期模块、通信方式、部署和工程规范。
   - 不代表所有模块都要在当前阶段实现。
2. [部署说明.md](部署说明.md)
   - Release 打包、`/cockpit-system` 目录、systemd、升级和回滚。
3. [architecture.md](architecture.md)
   - 当前代码对应的架构快照。
   - 记录已经落地的链路和最近开发顺序。
4. [实现状态.md](实现状态.md)
   - 模块级完成度、验证方式和下一步补齐项。
5. [项目进度总览.md](项目进度总览.md)
   - 当前阶段、下一步 3 件事和风险。
6. [项目范围与仓库策略.md](项目范围与仓库策略.md)
   - 当前项目范围和是否拆分仓库的决策。
7. [模块化策略.md](模块化策略.md)
   - 一个主仓库、多个内部模块以及未来拆库条件。
8. [配置说明.md](配置说明.md)
   - 参考 zcarcloud 后确定的 YAML 分区、类型化与校验规则。
9. [代码风格.md](代码风格.md)
   - 参考 zelos、znavigator 和 zcarcloud 的 C++ 代码风格约定。
10. [运行时通信策略.md](运行时通信策略.md)
   - 车端 runtime 通信分层：函数调用、队列/Actor、共享内存、gRPC/外部通信。
11. [变更记录.md](变更记录.md)
   - 每批改动的中文记录。
12. [WSL开发路线图.md](WSL开发路线图.md)
   - 当前无完整硬件条件下仍可实现和验证的开发批次。

## 参考与专项

- [参考项目.md](参考项目.md)：旧项目与开源项目参考。
- [参考代码审计.md](参考代码审计.md)：旧代码细粒度审计。
- [语音AI规划.md](语音AI规划.md)：麦克风、扬声器、语音与大模型规划。

## 目录放置速查

```text
cockpit/             产品源码根目录
  core/              配置、日志、生命周期和 IPC
  modules/           vehicle、can、audio、camera、voice
  drivers/           SocketCAN、ALSA、V4L2
  services/          设备所有者和长驻进程
  apps/              Qt/QML 车机应用
  proto/             服务间接口契约
tools/               调试工具、仿真器和 probe
configs/             运行配置和 systemd 示例
```

## 当前仓库策略

```text
当前：cockpit-system 一个主仓库
内部：core 基础设施 / modules 领域能力 / drivers 硬件适配 / proto 接口契约
未来：出现独立部署和发布边界后，再考虑 cloud-backend / cloud-frontend
```

目前不创建云端前后端仓库。Jetson 本机车辆链路、UI、调试工具和服务接口稳定后，
再根据实际需求决定是否拆分。

## 维护规则

- 代码行为变化后更新 `实现状态.md`。
- 架构边界变化后更新 `architecture.md`。
- 每批修改都追加到 `变更记录.md`。
- 总体蓝图与当前实现冲突时，以 scope、implementation status 和当前代码为准，并回头修订蓝图。
- 提交前运行 `pre-commit run`，或执行 `pre-commit install` 启用 Git hook。
- 静态检查手动运行 `pre-commit run clang-tidy --hook-stage manual -a`，不作为普通 commit 强制门禁。
