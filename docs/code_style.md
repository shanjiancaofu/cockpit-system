# 代码风格说明

本项目的 C++ 风格严肃参考 `zelos/znavigator` 和 `zelos/zcarcloud`，但不照搬其公司内部版权头、私有
pre-commit 源、Bazel 历史配置或 zlog/zutil 私有依赖。

## 基础规则

- C++ 源文件使用 `.cc`，头文件使用 `.h`。
- clang-format 基于 Google style，列宽 100，指针左贴，短函数和 lambda 不压成单行。
- namespace 使用多层展开写法，不使用 C++17 nested namespace 简写。
- include 分组顺序参考 zelos：系统头、第三方头、项目头；项目内 include 使用 `cockpit/...` 绝对路径。
- 文件、目录和 target 尽量使用浅显小写名称；已有历史类名保持兼容，不为了风格单独大规模重命名。

## 类接口

- public 生命周期和外部接口放前面，private helper 和成员变量放后面。
- 不可拷贝类使用 `COCKPIT_DISALLOW_COPY_AND_ASSIGN(TypeName)`，风格对应 zelos 的
  `DISALLOW_COPY_AND_ASSIGN`。
- 析构函数需要表达生命周期语义：持有线程、文件、socket、共享内存、gRPC server 的类必须在析构或
  `Stop()/Shutdown()` 中释放资源。
- 简单数据结构使用 `struct`，默认成员初始化；有不变量或资源生命周期时使用 `class`。

## 错误处理

- 底层模块优先返回 `bool`，通过 `std::string* error` 输出错误原因，避免在实时路径抛异常。
- 进程启动、配置加载、文件系统初始化等非实时路径可以使用异常，但要在服务边界转成日志和非 0 退出码。
- 弱依赖失败只记录 warning，不阻断主流程；强依赖失败应返回错误或进入 faulted 状态。

## Runtime 与服务边界

- `core/runtime` 只放生命周期、依赖图、模块编排等通用运行时能力，不放车辆、音频、相机、语音业务。
- `services/*` 是长运行进程入口和控制面；真正可复用逻辑优先沉到 `modules/*` 或 service 子模块。
- 同进程低频事件用 `MessageBus`；大块数据不走 MessageBus/gRPC，使用 shared memory 或文件句柄。

## 工具

- `CPPLINT.cfg` 保持与 zelos 一致的核心过滤项，当前不把 cpplint 放入普通 pre-commit，因为开发环境
  未默认安装 cpplint。
- 普通提交执行 pre-commit 基础检查和 clang-format；clang-tidy 保持 manual，按模块分批清理。
