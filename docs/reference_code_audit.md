# 旧项目代码细化审计

本文是在 `docs/reference_projects.md` 之后的细粒度代码审计，目标是把旧项目中可沉淀的工程经验转成 `system` 后续编码规则。

## 审计范围

已重点阅读：

- `zelos/zcarcloud/zcarcloud/carcloud`
  - `main.cc`
  - `carcloud.cc`
  - `common/config_manager.cc`
  - `runtime/listener_runtime.cc`
  - `event/mail_box.cc`
  - `transfer/handler_manager.cc`
  - `transfer/tcp/tcp_handler.cc`
  - `transfer/mqtt/mqtt_handler.cc`
  - `client/vehicle_cloud_api.cc`
  - `client/vehicle_config_update_workflow.cc`
  - `proto/config.proto`
- `zelos/car_cloud_server`
  - `cmd/server/main.go`
  - `internal/httpapi/router.go`
  - `internal/httpapi/errors.go`
  - `internal/service/deploy_service.go`
  - `internal/service/status_service.go`
  - `internal/storage/file/config_store.go`
  - `schema.sql`
  - `vehicle_cloud_api.proto`
- `zelos/safe_ota/safe_ota`
  - `main.cc`
  - `examiner/gw_request.cc`
  - `action/safe_ota_factory.cc`
- `vehicle-system`
  - `src/libs/runtime/Logger.cpp`
  - `src/service/runtime/AppProcessService.cpp`
  - `src/app/MainWindow.cpp`
  - `src/apps/camera_v4l2/v4l2.cpp`
  - `src/apps/weather/httpdownload.cpp`

## 1. 服务生命周期

### 可参考代码

- `zcarcloud/carcloud/main.cc`
- `zcarcloud/carcloud/carcloud.cc`
- `safe_ota/safe_ota/main.cc`

### 观察

`zcarcloud` 的入口模式是：

```text
InstallSignalHandler
ParseCommandLineFlags
Create CarCloud
Start
WaitForShutdown
Stop
Shutdown flags
Restore signal
```

`CarCloud::Initialize()` 内部进一步拆成：

```text
InitEnv
InitDir
ReloadLogger
Create business service
```

### 对 system 的规则

当前 `common/runtime/ServiceRuntime` 已吸收了：

- 命令行解析
- 配置加载
- 日志初始化
- SIGINT/SIGTERM 退出标记

后续应继续补齐：

- `InitDir()`：按配置创建日志、数据、缓存、下载目录。
- `ServiceMain` 模板：统一 `Init -> Start -> Wait -> Stop`。
- `Stop()` 必须可重复调用，析构函数可安全调用 `Stop()`。
- 长线程必须有退出条件，并在 `Stop()` 中 join。

## 2. 配置系统

### 可参考代码

- `zcarcloud/common/config_manager.cc`
- `zcarcloud/proto/config.proto`
- `car_cloud_server/internal/config/config.go`

### 观察

`ConfigManager::ReloadConfig()` 的回滚逻辑很重要：

```text
保存 previous_config
清空当前 config
ReloadPlatformConfig
ReloadVehicleConfig
失败则恢复 previous_config
```

Go 车云服务采用：

```text
Default()
Load(file)
文件字段覆盖默认值
```

### 对 system 的规则

短期：

- 保留当前轻量 YAML reader，只用于 smoke。
- 继续使用“默认值 + 文件覆盖”的模式。

中期：

- 用 `yaml-cpp` 替换手写解析。
- 增加 typed config：
  - `VehicleConfig`
  - `CanConfig`
  - `GatewayConfig`
  - `MqttConfig`
  - `LoggingConfig`
  - `PathConfig`
- `ReloadConfig()` 必须支持失败回滚。
- 配置路径不要散落在业务代码里，应集中在 `common/config`。

## 3. 日志系统

### 可参考代码

- `vehicle-system/src/libs/runtime/Logger.cpp`
- `zcarcloud/carcloud/carcloud.cc`

### 观察

Qt 日志实现中值得吸收的点：

- 每个应用独立日志文件。
- `*.log.1` 简单滚动。
- 环境变量覆盖日志目录。
- 可镜像到 stderr。
- 重复日志压缩：连续相同消息累计到 50 次再输出 summary。

### 对 system 的规则

当前 `common/logging` 已支持：

- 每服务独立日志文件。
- 简单滚动。
- stderr 输出。

后续应补：

- 重复日志压缩。
- 按模块动态日志等级。
- 高频路径只打统计，不逐帧 INFO。
- token、证书、私钥、车辆隐私字段禁止明文日志。

## 4. 组件发现与运行时

### 可参考代码

- `zcarcloud/runtime/listener_runtime.cc`
- `safe_ota/action/safe_ota_factory.cc`

### 观察

`ListenerRuntime` 使用配置里的 `type()` 创建具体 listener：

```text
LocalizationListener
VehicleMotionListener
VehicleBatteryListener
GpsListener
RoutingListener
```

不支持的类型会明确打 ERROR 并失败。

`SafeOtaFactory` 用 mode 选择业务类，简单直接。

### 对 system 的规则

后续服务内部组件可以采用“配置驱动 + 显式 factory”：

- `vehicle-data-service`：
  - `mock`
  - `socketcan`
  - `replay`
- `camera-service`：
  - `v4l2`
  - `gstreamer`
  - `mock`
- `cloud-uplink-service`：
  - `disabled`
  - `mqtt`
  - `http`

不要一开始做复杂插件系统，先用清晰 factory。

## 5. 事件与线程模型

### 可参考代码

- `zcarcloud/event/mail_box.cc`
- `zcarcloud/proto/config.proto`

### 观察

`MailBox` 将事件拆成：

- init event
- time event
- trigger event
- serial task
- parallel task

线程模型：

```text
Start: running=true, std::thread(EventLoop)
EventLoop: while running -> task.Run -> sleep 10ms
Stop: running=false, join
```

### 对 system 的规则

短期不引入完整事件系统，但可以吸收以下规则：

- 服务线程必须集中管理。
- 线程名、退出标志、join 都要明确。
- 后续 `vehicle-data-service` 可分成：
  - CAN read thread
  - decode thread
  - publish thread
- 队列要有容量上限，满了打 WARN 和统计，不无限增长。

## 6. 传输层设计

### 可参考代码

- `zcarcloud/transfer/handler_manager.cc`
- `zcarcloud/transfer/tcp/tcp_handler.cc`
- `zcarcloud/transfer/mqtt/mqtt_handler.cc`

### 观察

`HandlerManager` 的复用规则很有价值：

- 根据 `NetworkConfig` 类型选择 handler。
- 根据 host/port/uri 构造 connection key。
- 根据配置 DebugString + CRC 判断配置是否变化。
- 配置不变复用 handler。
- 配置变化先 Stop 旧 handler，再创建新 handler。

TCP handler 的工程细节：

- DNS 解析。
- socket RAII 需要后续补强，但关闭逻辑明确。
- 非阻塞 connect + `poll` 等待。
- send 处理部分写、EINTR、EAGAIN/EWOULDBLOCK。
- 失败后 reconnect，最多重试 3 次。

MQTT handler 的工程细节：

- 先过滤有效 publish flow。
- topic、URI、client id 必须校验。
- 证书内容先 materialize 到文件，再交给客户端。
- 不支持的 read path 明确返回 false。

### 对 system 的规则

后续 `cloud-uplink-service`：

- 先定义 `TransportHandler` 接口：
  - `Init()`
  - `Send(payload, topic/channel)`
  - `Stop()`
- MQTT handler 必须校验：
  - broker URI
  - client id
  - publish topic
  - QoS
  - TLS 文件或内容
- handler 复用不能只看名字，要看连接关键参数和配置 fingerprint。

后续 `common/net`：

- 把 TCP/poll/partial-write/reconnect 模式沉淀成工具类。
- 所有 fd 用 RAII 包装。

## 7. 车云配置拉取

### 可参考代码

- `zcarcloud/client/vehicle_cloud_api.cc`
- `zcarcloud/client/vehicle_config_update_workflow.cc`
- `car_cloud_server/vehicle_cloud_api.proto`
- `car_cloud_server/internal/service/deploy_service.go`

### 观察

C++ 车端流程：

```text
PullConfig(checksum)
如果云未开启 -> cloud disabled
如果平台配置需要更新 -> 下载平台包到 .tmp -> rename
如果车辆配置需要更新 -> PullVehicleConfig -> 原子写文件
CheckDownloadStatus
ReloadConfig
```

Go 服务端规则：

- 未找到车辆、车云未开启、未绑定平台时，车辆侧 pull 返回 `cloud_enabled=false`。
- 开启车云前必须绑定平台且平台配置存在。
- 上传配置必须校验 checksum。
- 车辆配置必须是合法 JSON。
- 批量操作先全量校验，再更新，避免部分成功。

### 对 system 的规则

后续 `cloud-uplink-service` 应分成两个子模块：

```text
cloud/
  VehicleCloudClient      # HTTP/MQTT 客户端封装
  ConfigUpdateWorkflow    # 配置拉取/下载/安装事务
  TelemetryReporter       # 状态上报
```

配置安装必须使用事务：

```text
download to .tmp
verify non-empty/checksum
extract/write staged files
atomic rename
reload config
failure cleanup
```

禁止直接覆盖正在使用的配置。

## 8. HTTP/protobuf 客户端

### 可参考代码

- `safe_ota/examiner/gw_request.cc`
- `zcarcloud/client/vehicle_cloud_api.cc`

### 观察

`GwRequest` 支持：

- access token 获取。
- token header 自动注入。
- token 失效错误码触发刷新并重试一次。
- JSON response 和 protobuf/octet-stream response 双解码。
- 反射检查 `success`、`message`、`error_code` 字段。

风险：

- 旧代码会打印 access token，新工程不要这么做。

### 对 system 的规则

后续 `common/http` 或 `services/cloud-uplink-service/client`：

- token 不落日志。
- 失败日志只打错误码、HTTP status、URL path，不打完整敏感 header。
- JSON/protobuf 解码可共用，但响应 envelope 要显式建模。
- token refresh 必须带锁，避免多线程同时刷新。

## 9. Go 车云服务端设计

### 可参考代码

- `car_cloud_server/internal/httpapi/router.go`
- `car_cloud_server/internal/httpapi/errors.go`
- `car_cloud_server/internal/service/status_service.go`
- `car_cloud_server/schema.sql`

### 观察

值得吸收：

- 管理端 response 带 `trace_id`，车辆端 response 更简洁。
- 错误统一映射，不在 handler 里散落 HTTP 状态。
- `timestamp_ms` 解析非常严格，能拒绝小数、非法指数、超范围值。
- 在线状态只保留内存快照，用 `offlineTimeout` 判断是否在线。
- schema 使用 `platforms`、`vehicles` 两张表，配置内容以 blob + checksum 存储。

### 对 system 的规则

后续 `system-monitor-service` 和 `cloud-uplink-service`：

- 本地状态快照可以先内存化。
- 上报时间戳统一 `int64 timestamp_ms`。
- 所有外部输入都要严格校验，不接受“差不多能解析”的格式。

后续 `common/database`：

- 本地 SQLite 表也采用 metadata + content/checksum 的结构。
- 外部路径不要直接作为数据库唯一事实。

## 10. Qt 与硬件 demo

### 可参考代码

- `vehicle-system/src/app/MainWindow.cpp`
- `vehicle-system/src/service/runtime/AppProcessService.cpp`
- `vehicle-system/src/apps/camera_v4l2/v4l2.cpp`
- `vehicle-system/src/apps/weather/httpdownload.cpp`

### 可吸收内容

- Qt 主壳可以用页面栈 + 底部导航做迁移期 UI。
- `AppProcessService` 作为旧应用迁移桥可用。
- V4L2 ioctl 顺序可参考：
  - open
  - query capability
  - enum format
  - set format
  - request buffers
  - mmap
  - queue buffers
  - stream on
  - dqbuf/qbuf loop
  - stream off
  - munmap/close

### 不能照搬内容

- V4L2 demo 硬编码 `/dev/video1`。
- UI timer 中直接 `VIDIOC_DQBUF`，可能阻塞 UI。
- `userbuff`、fd、stream 状态缺少完整 RAII。
- `exit(1)` 直接退出程序，不适合主工程。
- 天气 demo 网络错误处理不足，`QNetworkReply` 没有完整错误/释放处理。
- 天气 demo 中 `type[1]` 被赋值两次，`type[0]` 未赋值，是典型迁移风险。

### 对 system 的规则

后续 `camera-service`：

- V4L2 采集必须在服务线程，不在 UI 线程。
- device path、format、width、height、fps 从 YAML 读取。
- fd、mmap buffer、stream state 用 RAII 管理。
- DQBUF 要配合 poll/select 和超时。
- UI 只拿 metadata 或渲染 surface，不直接操作 ioctl。

后续 `weather-service`：

- 网络超时、HTTP status、JSON parse error 都要显式建模。
- API 地址和城市码从配置读取。
- 缓存最后一次成功结果。

## 11. 已反向影响 system 的代码

当前已经落地：

- `.cc` 后缀统一，贴近 zelos C++ 风格。
- `common/runtime` 吸收服务启动、配置、日志、信号退出。
- `common/logging` 吸收每服务独立日志和滚动。
- `common/proto/cloud.proto` 吸收车辆状态上报、配置拉取字段。
- `docs/reference_projects.md` 记录高层复用判断。

## 12. 建议的下一批代码任务

优先级从高到低：

1. `common/can`
   - `CanFrame`
   - `SocketCanReader`
   - `SocketCanWriter`
   - fd RAII
   - poll timeout
2. `tools/can-simulator`
   - 支持 `--backend stdout|socketcan`
   - socketcan 模式向 `vcan0` 发送真实 CAN frame
3. `vehicle-data-service`
   - 支持 `vehicle.source=mock|socketcan`
   - socketcan 模式从 `vcan0` 读取并解析 VehicleState
4. `common/logging`
   - 重复日志压缩
   - 配置 `mirror_stderr`
5. `common/config`
   - 引入 `yaml-cpp`
   - typed config structs
6. `cloud-uplink-service`
   - 先实现 HTTP config pull workflow
   - MQTT telemetry 放到配置拉取之后

