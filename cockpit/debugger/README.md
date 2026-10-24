# debugger

职责：日志、故障诊断、调试信息与 Jara 上报。

logging/ 持有 Logger 实现，日志接口属于 exchange/foundation/logging；保留 debugger ABI 骨架，Jara 上报未实现。

头文件与实现在对应功能目录内组织。跨模块仅依赖已声明的稳定接口、生成的协议合同或 IPC，不引用其他模块的私有实现。
源码目录不决定进程数量，现有 ABI/模块名及进程组合保持兼容。
