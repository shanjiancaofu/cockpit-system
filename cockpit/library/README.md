# library 目录职责

`cockpit/library` 保存由 Navigator 装载的进程级模块。这里的代码负责组合领域能力、
持有模块运行时资源，并提供统一的启动、停止、轮询和 IPC 接口；它不是通用工具库，
也不是独立的 service 层。

主要目录：

- `driver/`：硬件相关模块的进程入口与运行时生命周期。
- `agent/`：语音交互和后续 AI 能力的编排入口。
- `hmi/`：HMI 进程级运行时。
- `recording/`：运行数据记录。
- `transfer/`、`carupload/`：数据传输与上传。
- `calibration/`、`debugger/`、`upgrader/`、`watchdog/`：对应的进程级能力。

目录边界：

- 可复用的领域逻辑放在 `cockpit/modules`。
- ALSA、V4L2、SocketCAN 等底层平台适配放在 `cockpit/drivers`。
- 只有需要被 Navigator 独立装载、管理生命周期或暴露 IPC 的组合代码放在这里。
