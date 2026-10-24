# Exchange Foundation

这里只存放稳定基础实现：base、build、config、event、health、ipc、json、runtime、time。
头文件与实现在 `cockpit/exchange/foundation/` 对应功能目录内组织；logging 接口也在这里，实现归 debugger/logging。
每个调用方链接需要的最小 target，不要求所有流量经过 exchange。
不得依赖任何业务模块私有实现。ProcessRuntime 是工具/轻量模块的基础，不是第二个 Navigator。
