# Camera V2 PR-05A：Native Hardware ISP 控制

本轮基于 `6ef4b10`，实现隔离 Native Runtime 的 AE/AWB、曝光、模拟增益、ISP 数字增益、
曝光补偿、抗频闪和手动 WB 控制。实现位于 `cockpit/driver/camera/tuning/` 和现有
`capture/native_argus_capture.*`，CLI 复用 `camera-ctl`。

生产默认仍为 GStreamer，软件 ISP 仍使用独立 RAW/NEON 测试路径。
Profile 持久化、降噪/锐化/色彩控制属于 PR-05B，正式预览接入属于 PR-06。

## 接口和生效语义

`NativeArgusCapture` 新增 `GetTuning`、`SetTuning`、`ResetTuning`。
所有控制调用与 Start/Acquire/Stop 共用串行锁；释放帧仍沿用原有返回队列。
每次更新创建独立 Request，设置全部参数、Sensor Mode、固定帧周期、输出流及 clientData，
再提交 repeat。创建或设置失败不会修改旧 Request；repeat 拒绝时重提旧 Request，恢复失败则
拒绝继续 Acquire/Set，等待 Stop 清理。Stop 后或持帧关闭期间拒绝设置。

`CameraTuningState` 明确区分：

- `requested`：应用提交的完整参数。
- `accepted`：从新 Request 的 SDK getter 读回的参数，不表示某一帧已经生效。
- `last_observed`：最近一次 Acquire 取得的实际 CaptureMetadata；首次 Acquire 前 valid=false。
- `defaults`：当前 Session 创建时读取并限制在当前 Sensor Mode/帧周期内的默认值，供 Reset 恢复。

`NativeFrame::tuning` 保存逐帧实际曝光 ns、Sensor Gain、ISP Gain、R/Gr/Gb/B WB Gains、
AE/AWB Mode、AE/AWB Lock 和 AWB CCT。
revision 取自 `ICaptureMetadata::getClientData()`，与 Request clientData 一一对应。
更新后仍可能收到旧 Request 的在途帧，消费者必须联合 `session_id + revision` 判断来源，
不能把最新 requested 值写到旧帧上。旧 Lease 的 Metadata 是值拷贝，更新不能改写它。
revision 从每个新 Session 的 1 开始；准备更新时消耗唯一编号，失败提交的编号不复用，
避免可能已进入 daemon 的失败请求与后续请求混淆。达到 uint32 上限会拒绝更新。

设置成功表示 SDK 接受并排队。手动曝光存在 Sensor 行周期量化，实际效果必须检查 Metadata。
CLI 验证允许曝光 5% 或 100us（取较大值）的误差，增益/WB 允许 5% 或 0.01 的误差。
抗频闪和 EV 查询来自 Request getter；当前 SDK 没有本接口所需的逐帧抗频闪设置值或 EV 读回，
不把请求值伪装成逐帧 Metadata。

实现依据本机 R36.5.2 SDK 的 Argus/Settings.h、CameraDevice.h、Request.h 和 CaptureMetadata.h；
参考 [NVIDIA ICaptureSession 文档](https://docs.nvidia.com/jetson/archives/r36.4/ApiReference/classArgus_1_1ICaptureSession.html)。
在线参考版本与本机头文件版本不同，具体能力以本机 SDK 和实测为准。

## 参数验证和能力边界

- 数值范围来自所选 Sensor Mode 与 CameraProperties。曝光上限同时受固定帧周期限制。
- AE auto 下可设置曝光/增益范围；manual 要求曝光、模拟增益、ISP Gain 都为固定值，AE 不锁定且 EV=0。
- 自动增益由 AE 在给定范围内求解，不新增虚构的独立 AGC 模式。
- WB 顺序为 R/Gr/Gb/B；manual 要求有限正数，并关闭 AWB Lock。SDK 未提供完整的 WB Gain 上限查询。
- 模式列表标记为 SDK 候选值，SDK 没有完整的设备支持模式枚举。未知模式、本地越界和 SDK 拒绝都明确报错。
- 参数读取与实际帧验证分别记录；未在当前设备运行的模式不据此宣称已经验证。

本机 IMX219、1080P30 查询到曝光 13,000–33,333,334ns、Sensor Gain 1–10.625、
ISP Gain 1–256、EV -2–2。该曝光范围包含应用为固定帧周期施加的上限。

## CLI：隔离采集

先停止占用相机的 Camera Service/Probe。`tuning` 子命令需要显式 `--native`，
会创建自己的 Native Session，与现有 gRPC 命令分开；参数仅在本次命令的 Session 内有效，
不会配置后台 CameraService，也不会在下次命令自动恢复前一次设置。

```bash
cmake --build _output/build/arm64-debug --target camera-ctl \
  native_camera_tuning_hardware_test native_argus_capture_hardware_test -j4
mkdir -p _output/runtime/camera-pr05

_output/build/arm64-debug/bin/camera-ctl tuning get --native \
  --report _output/runtime/camera-pr05/get.json

_output/build/arm64-debug/bin/camera-ctl tuning set --native \
  --exposure-us 8000 --gain 2 --wb-gains 1.5,1,1,1.25 \
  --report _output/runtime/camera-pr05/manual.json

_output/build/arm64-debug/bin/camera-ctl tuning set --native \
  --ae auto --awb auto --ev 1 --antibanding 50hz \
  --report _output/runtime/camera-pr05/auto.json

_output/build/arm64-debug/bin/camera-ctl tuning reset --native \
  --report _output/runtime/camera-pr05/reset.json

_output/build/arm64-debug/bin/camera-ctl tuning verify --native \
  --frames 30 --warmup 30 --cycles 10 \
  --report _output/runtime/camera-pr05/verify.json
```

`set` 先在同一个 Session 内采集 warmup 帧，然后动态更新，等待对应 revision 的帧。
manual CLI 同时要求 `--exposure-us` 与 `--gain`，默认固定 ISP Gain=1；
范围参数为 `--exposure-range-us min,max`、`--gain-range min,max`，锁定使用 `on|off`。
`verify` 在同一 Runtime 内重复自动预热、手动曝光/增益/WB、恢复自动、EV/50Hz、EV/60Hz、
AE/AWB 锁定、Reset 和非法增益拒绝，完整记录每帧来源与 Metadata。
退出码 0=通过，1=SDK/采集/验证失败，2=命令参数或报告输出错误。`--report` 保存 JSON。

## 测试入口

```bash
ctest --test-dir _output/build/arm64-debug \
  -R '^camera_tuning(_stub|_cli)?_test$' --output-on-failure

_output/build/arm64-debug/bin/native_camera_tuning_hardware_test \
  --output _output/runtime/camera-pr05/concurrent.json

# 正常路径验收；保留现有超时恢复与生命周期测试，不注入 SIGKILL。
python3 scripts/tests/jetson-camera-v2-gate.py --normal-only --tuning \
  --cycles 10 --continuous-frames 3000 \
  --output-dir _output/runtime/camera-pr05/gate
```

单元测试覆盖帧周期限制、范围/模式、非有限值、手动组合、停止状态和 SDK-disabled stub；
CLI 参数反例在打开硬件前拒绝。硬件线程测试同时 Acquire/更新，验证 revision、旧 Lease、
Stop 持帧及 Session 重建。硬件用例不注册为自动 CTest。

## 独立风险

Native STREAM_TYPE_BUFFER 的 SIGKILL/daemon 崩溃仍是生产切换阻塞项。本轮没有执行异常退出注入，
不改写 PR-03/04 的历史失败证据，不用正常启停通过来替代异常退出验收。
本轮环境是单一现场光照，不代表完整 IQ、可控光照、长期稳定性或标定验收。

## 2026-10-26 实测验收

本机 R36.5.2 / Argus 0.99.3.3 / IMX219：

| 检查 | 结果 |
| --- | --- |
| 完整 Debug 构建 | 通过 |
| 全量 CTest | 105/105，通过，含 SDK-disabled stub |
| Native 连续采集 | 1080P，3000 帧，30.006364 Sensor FPS，Capture ID 缺口/重复/回退均为 0 |
| 10 次正常启停、超时恢复 | 通过，daemon InvocationID 保持一致 |
| 最终版 10 轮 ISP 控制 | 70 个阶段；Acquire/Return/Release 均为 2400，Surface 分配/释放均为 60 |
| 手动逐帧验证 | 8,000,000ns、Sensor Gain=2、ISP Gain=1、WB=[1.5,1,1,1.25] |
| AE/AWB 锁定 | 10 轮各 30 帧均报告锁定，曝光、Sensor Gain 和 WB Gains 保持一致 |
| 并发 Acquire/Control | 通过，含旧帧 Metadata、新 Session 和持帧 Stop 检查 |
| 自动恢复、EV/50Hz/60Hz、非法参数拒绝 | 通过；EV/抗频闪由 Request getter 验证 |

连续采集的进程 CPU 为 2.577288%、RSS 见连续采集 JSON，仅覆盖原生 NV12、扩展 Metadata 和首帧采样，
不包含 VIC、Qt 或编码，不与 BGRx 完整业务链路作等价比较。

完整正常路径 Gate 先于最后的提交编号加固采集；加固后的最终二进制重新执行了 10 轮控制和线程专项。
各组二进制 SHA 分别记录，正常 Gate 与最终控制专项分别存档。
见 [总体验收](verification/cockpit/camera-pr05a/camera_pr05a_acceptance.json)、
[最终控制阶段](verification/cockpit/camera-pr05a/tuning-controls.json)、
[2400 帧 Metadata](verification/cockpit/camera-pr05a/tuning-frames.tsv) 和
[正常路径 Gate](verification/cockpit/camera-pr05a/camera_v2_gate.json)。
