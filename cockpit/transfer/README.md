# transfer

职责：网页与远程传输能力。

runtime/ 保留现有 gateway gRPC 及状态客户端；WebRTC/MQTT/WebSocket 后续实现。

头文件与实现在对应功能目录内组织。跨模块仅依赖已声明的稳定接口、生成的协议合同或 IPC，不引用其他模块的私有实现。
源码目录不决定进程数量，现有 ABI/模块名及进程组合保持兼容。
