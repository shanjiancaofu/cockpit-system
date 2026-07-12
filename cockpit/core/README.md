# core

与具体业务无关的基础设施：

- `config`：类型化 YAML 配置和启动校验。
- `event`：有界进程内事件队列。
- `ipc`：POSIX shared memory 映射生命周期。
- `logging`：统一日志。
- `runtime`：参数、信号、服务和模块生命周期。
- `time`：主机时间戳。

`core` 不存放车辆、音频、相机、语音或硬件业务代码，也不作为大而全的聚合 target。
