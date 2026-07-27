# cockpit-system 全仓库代码审核报告

## 1. 审核结论

**结论：Request changes。**

仓库的分层、模块 ABI、进程隔离、配置校验和测试组织已经具备较好的工程基础，但当前版本仍存在多项会影响升级正确性、进程唯一性、磁盘安全和并发稳定性的高严重级问题。若用于无人值守设备或量产部署，建议先修复本报告中的 P1 问题，并把两项安全问题作为上线阻断项处理。

本次共整理：

- **P1 高严重级：6 项**
- **P0/上线安全阻断：2 项**
- **P2 中严重级：8 项**
- **P3 工程改进：若干**

## 2. 审核范围与方法

- ZIP：`cockpit-system-main.zip`
- SHA-256：`420f4f039c1b9eb6b36efbede53e5550f225a25fa96c4310c2c11cc545a2f8a0`
- 文件总数：454
- 目录总数：124
- 主要技术：C++17、CMake、gRPC、POSIX 共享内存、动态库模块、systemd、Qt/QML、摄像头、音频、录制、OTA
- 测试：约 46 个 C++ 测试源文件，CMake 中注册约 47 个 CTest

本次对全部文件完成了目录清点、文件类型统计、敏感信息扫描、构建引用扫描和静态检索；对 Navigator、进程管理、IPC、共享内存、Camera、Recording、Upgrader/OTA、systemd、配置及测试进行了逐文件重点审查。

本报告是**静态审核**。没有执行仓库脚本、没有修改仓库，也没有完成构建和动态测试。ZIP 不包含 `.git` 历史，同时构建依赖 `third_party/cockpit-unified-deps/prefix` 和 `third_party/sherpa-onnx`，因此无法仅凭该 ZIP 复现完整构建。

---

## 3. P1：高严重级问题

### P1-1 OTA 健康检查运行的是混合版本，可能错误确认升级

**位置：**

- `configs/systemd/cockpit-navigator@.service:10-11`
- `cockpit/navigator/main.cc:73-95`
- `cockpit/navigator/process/process_manager.cc:66-73`
- `cockpit/navigator/process/process_manager.cc:251-274`
- `cockpit/navigator/process/process_manager.cc:354-360`
- `cockpit/library/hmi/hmi_runtime.cc:39-49`
- `scripts/deploy/install.sh:46-50`
- `tools/safe-ota/action/safe_ota.cc:110-175`
- `scripts/deploy/healthcheck.sh:8-38`

**问题：**

升级激活通过切换 `/current` 符号链接完成，但 Safe OTA 在激活后只将 Navigator 从 `upgrade` 模式切回 `normal`，没有重启或重新执行 Navigator 主进程。

Navigator 启动时通过 `/proc/self/exe` 保存了当前真实可执行文件路径，因此激活后：

- Navigator 主进程仍是旧版本；
- module-child 仍由旧版本可执行文件启动；
- 动态模块路径引用 `/current/...`，会加载新版本 `.so`；
- HMI 根据 module-child 的 `/proc/self/exe` 推导 UI 路径，仍可能启动旧版本 UI。

这会形成“**旧 Navigator 核心 + 旧 module-child/UI + 新动态模块**”的混合运行时。健康检查只检查模式、模块和服务状态，不验证每个进程的 release/build ID，因此可能在新 Navigator 和新 UI 从未真正运行的情况下确认升级成功。

**影响：**

- Navigator/UI 中的升级修复未被验证；
- 同一 ABI 下混合版本可能表面正常、运行一段时间后才暴露兼容问题；
- 不同 ABI 会直接失败，但这只能覆盖部分不兼容情况。

**建议：**

1. 将 OTA 编排放在稳定的外部 supervisor 中；激活 `/current` 后由 systemd 完整重启 Navigator。
2. 健康检查必须校验 Navigator、module-child、动态模块和 UI 的统一 release ID/build ID。
3. 只有新版本完整进程树启动并通过健康检查后，才确认升级。
4. 增加 v1 → v2 集成测试，给两版二进制注入不同版本号，验证不存在混合版本。

---

### P1-2 OTA 子进程没有真实超时和取消，可能永久卡死或遗留孤儿安装器

**位置：**

- `cockpit/library/upgrader/upgrade_transaction.cc:122-154`
- `cockpit/library/upgrader/upgrade_transaction.cc:347-350`
- `cockpit/library/upgrader/upgrade_transaction.cc:533-548`
- `tools/safe-ota/action/safe_ota.cc:115-123`
- `cockpit/navigator/process/process_manager.cc:151-186`
- `cockpit/navigator/process/run_config.h:18-21`

**问题：**

`RunProcess()` 使用阻塞式 `waitpid(pid, ..., 0)`，没有：

- 单个子进程超时；
- 取消令牌；
- 子进程组；
- 终止整棵进程树；
- 父进程死亡联动。

健康检查的 deadline 只在两次命令执行之间检查。如果某一次健康检查命令自身卡住，整个升级线程会永久阻塞。

更严重的是，Navigator 在切换模式时只等待模块子进程约 3 秒。若 Upgrader 正在等待安装脚本：

1. Upgrader 的 Stop 尝试 join 工作线程；
2. 工作线程卡在 `waitpid()`；
3. Navigator 超时后 SIGKILL Upgrader module-child；
4. 安装脚本不属于受控进程组，可能继续运行；
5. Safe OTA 同时开始回滚，安装器与回滚发生竞态。

**影响：**

- 升级流程永久卡死；
- 留下孤儿安装进程；
- 安装和回滚并发修改同一 release/current 状态；
- 文件系统可能进入半安装状态。

**建议：**

- 使用 `posix_spawn()` 或严格封装的 fork/exec；
- 每次执行建立独立 process group；
- 使用 `waitpid(..., WNOHANG)` + monotonic deadline；
- 超时或取消时对整个进程组执行 SIGTERM，宽限后 SIGKILL；
- 子进程设置 parent-death signal 作为防御；
- 分别设置安装、校验、健康检查、回滚超时；
- Safe OTA 不应只以 module-child 是否退出作为安装动作已停止的证明。

---

### P1-3 多线程进程中 fork 后执行非异步信号安全代码，存在偶发死锁

**位置：**

- `cockpit/core/logging/logger.cc:55-74`
- `cockpit/navigator/main.cc:73-79`
- `cockpit/navigator/process/process_manager.cc:251-274`
- `cockpit/library/upgrader/upgrader_runtime.cc:18-64`
- `cockpit/library/upgrader/upgrade_transaction.cc:122-145`

**问题：**

Navigator 在初始化异步日志线程后继续通过 `fork()` 启动 module-child。子进程在 `exec` 前调用了 `std::to_string()`。Upgrader 同样在已有线程的进程中 fork，并在子进程中调用 `setenv()`。

多线程进程 fork 后，子进程只保留调用 fork 的线程，但继承其他线程可能持有的 libc/allocator 锁。在 exec 前调用内存分配、环境变量管理或 C++ 运行库函数，可能等待一个永远不会释放的继承锁。

**影响：**

- 低概率、难复现的 module-child 启动卡死；
- OTA 安装/健康检查子进程偶发卡死；
- 问题可能只在高并发日志或内存分配压力下出现。

**建议：**

优先改用 `posix_spawn()`。若暂时保留 fork：

- fork 前预先构造完整 argv/envp；
- 子进程仅调用 async-signal-safe 系统调用和 `execve()`；
- 不调用 `std::string`、`std::to_string`、`setenv`、日志和 malloc 相关函数。

---

### P1-4 Navigator Unix Socket 可被第二实例抢占，形成双实例 split-brain

**位置：**

- `cockpit/navigator/connection/ipc_connector.cc:88-124`
- `cockpit/navigator/connection/ipc_connector.cc:168-179`
- `tests/runtime/navigator_test.cc:75-110` 附近

**问题：**

IPC server 创建前无条件 unlink 已存在的 socket 文件，没有先探测该 socket 是否由正在运行的 Navigator 持有。

因此第二个 Navigator 可以：

1. 删除第一个实例仍在监听的路径名；
2. 在同一路径绑定自己的 socket；
3. 第一个实例继续运行并持有硬件/模块，但新客户端只连接第二个实例。

此外，第一个实例退出时会再次按路径 unlink，可能把第二个实例的 socket 删除。

**影响：**

- 同时存在两个 Navigator；
- 硬件、摄像头、共享内存和模块出现重复所有者；
- 控制请求连接到不同实例，产生 split-brain；
- 后退出的旧实例可破坏新实例的 IPC 入口。

**建议：**

- 使用独立 lock file + `flock(LOCK_EX | LOCK_NB)` 保证单实例；
- unlink 前先 connect 探测，仅在明确 `ECONNREFUSED` 且确认陈旧时清理；
- 记录创建 socket 的设备号和 inode，关闭时只删除自己创建的节点；
- 更稳妥的方案是 systemd socket activation 或 `RuntimeDirectory=` 管理；
- 增加两个 Navigator 同时启动、旧实例晚退出的集成测试。

---

### P1-5 录制总配额只在会话边界清理，活动会话可写满系统盘

**位置：**

- `configs/config.yaml:60-69`
- `cockpit/library/recording/recording_service.cc:18-34`
- `cockpit/library/recording/recording_service.cc:175-217`
- `cockpit/modules/recording/recording_catalog.cc:98-124`
- `cockpit/modules/recording/recording_session.cc:150-179`
- `cockpit/modules/recording/recording_session.cc:269-317`

**问题：**

`max_total_bytes` 的清理只在 Initialize、Start、Stop 等会话边界触发。活动会话写入消息或复制 data file 时，没有检查：

- 当前活动会话大小；
- 全局总大小；
- 文件系统剩余空间；
- 预留安全空间。

Catalog 清理只会删除已完成会话，无法限制一个持续增长的活动会话。数据文件接口还可以复制任意大小的源文件。

**影响：**

- 单个长时间会话可突破配置配额；
- 错误客户端可快速填满根分区；
- 日志、数据库、OTA、systemd 及整个设备可能失效。

**建议：**

- 每次 append/copy 前检查活动会话字节数和 `statvfs()` 剩余空间；
- 设置 per-session 最大字节数、最大时长和系统预留空间；
- 超限时停止录制并进入可诊断 fault 状态，而不是继续写；
- 周期性执行配额维护；
- 对复制文件先读取大小并纳入原子配额预留；
- 增加磁盘接近满、单文件超限和长会话测试。

---

### P1-6 Camera 状态查询与生命周期操作存在数据竞争，属于 C++ 未定义行为

**位置：**

- `cockpit/library/driver/camera/control/camera_service.cc:106-249`
- `cockpit/library/driver/camera/grpc/camera_grpc_service.cc:126-154`
- `cockpit/core/runtime/module_manager.cc:27-70`
- `cockpit/core/runtime/module.cc:34-67`
- `cockpit/core/runtime/module.h:44-45`

**问题：**

Camera Start/Stop/健康检查在 `lifecycle_mutex_` 保护下修改 `ModuleManager/BasicModule` 状态，但 GetStatus 只锁 `mutex_`，随后读取 ModuleManager 和 BasicModule 的普通非原子字段。

多个 gRPC RPC 可以并发执行，因此 GetStatus 可与 Start/Stop 同时读写同一字段。ModuleManager 本身没有内部同步，BasicModule 的状态字段也不是原子变量。

**影响：**

- 标准层面的 data race 和未定义行为；
- 状态读取不一致；
- 优化构建中可能出现难以解释的错误、崩溃或卡死。

**建议：**

- GetStatus 按与生命周期操作一致的顺序获取 `lifecycle_mutex_` 和状态锁；或
- 将 ModuleManager/BasicModule 设计为自身线程安全，明确所有状态访问契约；
- 使用 ThreadSanitizer 增加 Start/Stop/GetStatus 并发测试。

---

## 4. P0：量产/上线安全阻断项

仓库 README 明确说明当前是原型阶段，因此以下问题不一定是原型功能缺陷；但对于车载、机器人或无人值守设备，它们应当视为上线阻断项。

### P0-1 控制面 gRPC 无鉴权，systemd 服务默认以 root 运行

**位置：**

- `configs/systemd/cockpit-navigator@.service:6-13`
- `configs/config.yaml:17-69`
- `cockpit/library/recording/recording_grpc_service.cc:90-100`
- `cockpit/library/driver/camera/grpc/camera_grpc_service.cc:93-103`
- `cockpit/library/driver/audio/grpc/audio_grpc_service.cc:84-94`
- Vehicle、Transfer、Voice 等其他 gRPC server 同样使用 `grpc::InsecureServerCredentials()`

**问题：**

服务默认绑定 loopback，降低了远程暴露面，但任何本机进程都可以调用控制接口。systemd unit 没有 `User=`，通常会以 root 启动 Navigator 及其子模块。

录制 API 允许调用方传入服务器本地路径：

- `cockpit/proto/recording.proto:24-32`
- `cockpit/library/recording/recording_grpc_service.cc:149-166`
- `cockpit/modules/recording/recording_data_file.cc:10-12`
- `cockpit/modules/recording/recording_session.cc:269-317`

当前校验基本只要求路径非空，root 进程随后复制该本地普通文件。这构成 confused-deputy 风险：低权限本地进程可诱导 root 服务读取并复制其本来无权读取的文件。

**建议：**

- Navigator 和各模块使用专用非 root 用户/组；
- 本地控制优先使用 Unix-domain socket，并通过文件权限限定调用者；
- 对高权限操作增加身份和授权；
- data-file 来源必须限定在明确 allowlist 根目录，使用 `openat2()`/目录 fd 防止路径穿越和符号链接竞态；
- 增加 systemd hardening：`NoNewPrivileges`、`ProtectSystem`、`ProtectHome`、`PrivateTmp`、`CapabilityBoundingSet`、`RestrictAddressFamilies` 等；
- 若接口可能跨主机使用，再增加 mTLS。

---

### P0-2 OTA 只有校验和，没有可信签名，随后直接执行包内脚本

**位置：**

- `cockpit/library/upgrader/upgrade_transaction.cc:88-119`
- `cockpit/library/upgrader/upgrade_transaction.cc:312-316`
- `cockpit/library/upgrader/upgrade_transaction.cc:347-350`

**问题：**

升级包中的 manifest 负责提供 SHA-256，程序校验 payload 是否匹配该 manifest，但 manifest 自身没有可信数字签名。攻击者若能替换升级包，可以同时替换 payload 和 checksum，校验仍会通过。之后系统直接执行包内 `deploy/install.sh`。

**影响：**

- OTA 完整性校验不能证明发布者身份；
- 被替换的升级包可执行任意高权限脚本；
- 缺少 anti-rollback，旧的合法包也可能被重新安装。

**建议：**

- 使用设备内置公钥验证签名 manifest，例如 Ed25519；
- 引入版本单调策略和 anti-rollback；
- 在执行任何包内代码前完成签名、目标设备、版本和策略校验；
- 量产场景考虑 TUF/Uptane 风格元数据；
- 长期应尽量采用声明式安装器，避免直接执行升级包自带任意脚本。

---

## 5. P2：中严重级问题

### P2-1 录制 flush 计数是全局的，与“每个流每 10 条 flush”语义不一致

**位置：** `cockpit/modules/recording/recording_session.cc:20,100-179`

Vehicle、event 和 data-index 共用 `messages_written`。当不同流交错写入时，计数达到 10 的那一次只 flush 当前流，其他流可能远超过预期条数仍未 flush。进程崩溃时，数据丢失窗口大于设计值。

**修复：**为每个输出流维护独立计数，或按时间周期统一 flush 全部流。若要求断电持久性，还需 `fdatasync/fsync`。

### P2-2 录制完成状态缺少断电持久性保证

**位置：** `cockpit/modules/recording/recording_session.cc:182-226,320-412`

当前主要依赖 iostream flush/close、写 marker 和 rename，没有对文件和父目录执行 fsync。掉电后可能出现：完成标记存在但数据未落盘、rename 丢失或目录项状态不一致。

**修复：**使用 fd 写入并在关键点 `fdatasync/fsync`；rename 前同步临时目录，rename 后同步父目录；增加 fault-injection/掉电恢复测试。

### P2-3 拍照文件存在覆盖、同名碰撞和半文件问题

**位置：**

- `cockpit/library/driver/camera/photo/camera_photo_service.cc:33-80`
- `cockpit/modules/camera/photo/jpeg_encoder.cc:69-162`

默认文件名只精确到毫秒；并发请求或调用方指定相同文件名时可能覆盖。编码器直接写最终路径，失败时可能残留不完整 JPEG。

**修复：**UUID/原子序号、`O_EXCL`、临时文件写入、成功后 fsync + rename，失败时清理临时文件。

### P2-4 IPC 命令在 Unix stream 上只 read 一次

**位置：** `cockpit/navigator/connection/ipc_connector.cc:127-159`

Unix stream 不保证一条命令一次 read 完整返回。当前单次读取最多 4096 字节，可能把分片命令当完整命令解析，也会静默截断超长命令。

**修复：**循环读取到换行/明确帧尾，设置总 deadline 和最大帧长度；超限返回显式错误。

### P2-5 ProcessManager::Stop 忽略部分 kill/waitpid 失败

**位置：** `cockpit/navigator/process/process_manager.cc:151-186`

部分 `kill()` 返回值被忽略，最终 wait 失败后仍可能清空 pid，导致上层认为模块已经停止，但实际子进程状态未知。

**修复：**明确处理 ESRCH、ECHILD、EINTR 和其他错误；状态未知时不能直接清空所有权记录；统一使用进程组。

### P2-6 Camera 健康故障不会触发 Navigator 级重启

**位置：**

- `cockpit/library/driver/camera/control/camera_service.cc:210-243`
- `cockpit/library/driver/camera/camera_runtime.cc:155-160`
- `cockpit/navigator/navigator.cc:149-155`

Camera 健康检查失败后停止 preview 并标记 faulted，但 `CameraRuntime::Poll()` 始终返回 0。Navigator 只在 Poll 非 0 时重启模块，因此 Camera 可以长期停留在 faulted 状态，需人工恢复。

**修复：**明确策略：要么 Camera 内部做有界重试和退避；要么将不可恢复故障传播给 Navigator 触发进程重启。

### P2-7 共享内存陈旧 PID 可能因 PID 复用误判为仍有活跃拥有者

**位置：** `cockpit/modules/camera/shared_memory/shared_frame_buffer.cc:166-253`

代码以 `kill(pid, 0)` 判断存储的 owner PID 是否存活。若旧共享内存残留且 PID 已被无关进程复用，即使当前进程已经拿到独占 flock，仍会拒绝恢复。

**修复：**把独占锁作为主要所有权依据；或同时记录 boot ID 和 `/proc/<pid>/stat` start time，避免只依赖 PID。

### P2-8 缺少仓库内 CI，依赖复现边界不完整

**位置：**

- `.github/workflows/` 不存在
- `CMakeLists.txt:72-108`

仓库已有较多测试和静态检查配置，这是优点；但没有 CI 自动执行。构建依赖仓库外 third_party 目录，ZIP 本身不能完整复现。

**修复：**

- 固定依赖版本和校验值；
- 提供容器/工具链文件或可重复 bootstrap；
- CI 至少覆盖 GCC Debug、Release、ASan/UBSan、TSan、CTest、打包与 OTA 集成测试。

---

## 6. P3：工程改进项

1. `SystemConfig::Validate()` 对 `paths.run_dir`、POSIX shm 名称格式等约束不完整，而底层共享内存实现要求更严格，建议把失败前移到配置加载阶段。相关位置：`cockpit/core/config/system_config.cc:377-480`、`cockpit/core/ipc/shared_memory_region.cc:27-29`。
2. 日志输出文件打开失败时应显式报错或降级到 stderr，避免静默丢日志。
3. 对进程、共享内存、文件和 socket 的所有权建议形成统一 RAII/状态机约束，减少清理路径分散。
4. 为服务暴露统一 build ID、配置摘要和依赖版本，便于 OTA 健康检查和现场诊断。
5. 将“可恢复故障、需重启故障、需整机降级故障”统一建模，避免各模块自行解释 Poll/health 语义。

---

## 7. 做得较好的部分

- 目录和职责分层总体清楚：Navigator、library、modules、core、proto、tools 分工明确。
- 动态模块 API 带 ABI 版本和模块名校验，能阻止明显的不兼容加载。
- 模块使用独立进程，Navigator 具备重启窗口和 crash report 思路，优于全部功能堆在单进程。
- 多处队列和 RPC 已考虑容量、deadline 和 cancellation。
- 共享内存实现考虑了布局版本、容量计算溢出和 process-shared robust mutex。
- 配置解析对未知键和多个范围进行了严格校验。
- 测试数量和覆盖面在原型项目中较好，包含运行时、IPC、录制、相机和升级相关测试。
- OTA 已具备事务状态、原子符号链接切换和回滚骨架。
- 仓库中未扫描到明显硬编码密钥、令牌或私钥。
- 已配置 clang-format、clang-tidy、pre-commit 和 cpplint 相关规则。

---

## 8. 建议修复顺序

### 第一批：先解决运行时正确性

1. 重构 OTA 子进程管理：超时、取消、进程组、无孤儿进程。
2. 消除多线程 fork 后非安全调用，优先统一为 `posix_spawn()`。
3. 激活新版本后完整重启进程树，并校验所有组件 build ID。
4. 为 Navigator 增加可靠单实例锁和 socket 所有权校验。
5. 对活动录制实施硬配额和磁盘预留。
6. 修复 Camera 生命周期/状态并发访问。

### 第二批：满足设备上线安全要求

1. 全部服务降权运行并配置 systemd sandbox。
2. 将本地控制面迁移到受权限保护的 Unix socket，增加授权。
3. 限制录制 data-file 来源路径。
4. OTA 增加可信签名和 anti-rollback。

### 第三批：提高故障恢复和数据可靠性

1. 录制 fsync/目录同步和 per-stream flush。
2. 拍照原子落盘和防覆盖。
3. IPC 完整 framing。
4. Camera fault 自动恢复策略。
5. 共享内存 owner 身份增强。
6. 建立 CI、Sanitizer 和 OTA 故障注入测试。

---

## 9. 建议新增的关键测试

- **OTA 混合版本测试：**v1/v2 Navigator、module、UI 返回不同 build ID，激活后必须全部为 v2。
- **OTA 卡死测试：**安装脚本和健康检查永久阻塞，验证 deadline、进程组清理和回滚互斥。
- **OTA 签名测试：**payload、manifest、版本号任一被篡改均必须拒绝。
- **Navigator 双实例测试：**第二实例必须启动失败，旧实例退出不能删除新 socket。
- **Camera TSan 测试：**并发执行 Start、Stop、GetStatus、健康检查。
- **Recording 配额测试：**活动会话持续写、超大 data file、磁盘只剩预留空间。
- **Recording 掉电测试：**在 flush、marker、rename 各阶段注入崩溃，验证恢复结果。
- **Photo 并发测试：**同毫秒、同显式文件名、编码失败，确保无覆盖和半文件。
- **IPC 分片测试：**逐字节发送、超长帧、无换行、慢客户端和半关闭。

---

## 10. 最终判断

该项目不是“结构混乱、需要推倒重写”的状态。主要问题集中在系统边界：

- 版本切换与进程生命周期；
- 多线程进程的子进程管理；
- 单实例和 IPC 所有权；
- 持续写盘资源上限；
- 模块并发状态；
- 本地高权限控制面和 OTA 信任链。

这些问题适合在现有架构上逐项修复，不建议为了修复它们整体重写。修复 P1 和 P0 后，再补齐 sanitizer、故障注入和端到端升级测试，项目才适合进入长期无人值守运行阶段。
