# Camera V2 PR-05B：Tuning Profile V1

基于 `e5d7bbe`，为已有 Native Hardware ISP Tuning 增加版本化配置、严格解析、原子保存、
设备/模式/能力校验和应用失败处理。Profile 使用既有 CameraTuning，不新增 Tuning Framework。
生产默认配置继续使用 argus_isp/GStreamer，Profile 仅在显式调用时加载。

## 配置内容

实现位于 `cockpit/driver/camera/tuning/tuning_profile.h/.cc`，示例为
[`configs/camera/imx219_tuning.yaml`](../configs/camera/imx219_tuning.yaml)。

V1 文件包含四个必需字段：schema_version=1、name、target、settings。

- target：固定 backend=native_argus，设备索引、SDK Sensor Model、Sensor Mode 索引、宽/高/FPS。
- settings：完整的 AE/AWB Mode、Lock、抗频闪、曝光 ns 范围、Sensor/ISP Gain 范围、EV、R/Gr/Gb/B Gains。
- 不保存 Session ID、revision、FD 或某一帧自动算法得到的曝光值。

`MakeTuningProfile` 从 CameraTuningState.accepted 生成配置，即 SDK Request getter 的读回值。
AE auto 下保存的是允许范围，自动白平衡下保存的 WB 数值不会被误认为逐帧白平衡结果。

解析拒绝未知版本、未知/重复/缺失字段、多 YAML 文档、错误类型、无穷/NaN、反向范围、
非法手动参数组合和超过 64KiB 的输入。成功解析前不改写调用方已有 Profile 对象。
同时可加载 YAML 语法支持的 JSON 子集；保存统一写为 YAML。

本机 SDK 头文件提供 IDenoiseSettings、IEdgeEnhanceSettings，以及 AutoControl 的 Color Matrix/
Tone Map 接口。它们还未在此阶段完成设备能力及实际画质验收，因此没有纳入 V1，也没有虚构字段；
文件中加入 denoise/edge/tone_map 等未知字段会报错。

## 应用和回滚语义

`NativeArgusCapture::ApplyTuningProfile` 与 Start/Acquire/Set/Stop 共用控制锁。
它在锁内核对实际设备描述和当前 Sensor 能力，校验通过才调用既有 SetTuning；
设备、Model、Mode、分辨率或 FPS 不匹配，以及参数越界，都会在提交新 Request 前拒绝，
原 requested/accepted 和 revision 保持有效。

SDK 设置/提交失败沿用 PR-05A 的旧 Request 保护与恢复逻辑；恢复旧 repeat 也失败时，
Runtime 会拒绝继续 Acquire/Control，不能把这种情况报告为成功回滚。

应用接口成功只代表 SDK 接受并排队，仍需采集循环按 session_id/revision 验证实际 Metadata。
它不在后台创建第二条采集线程，也不替 Frame Broker 消费帧。
CLI `load` 在同一个隔离 Session 内等待新 revision 并验证；若提交成功后观测失败，
会重新提交加载前的 accepted 参数，并再次观测确认。rollback_attempted/rollback_succeeded
反映这一步结果；加载失败仍返回失败，不会因回滚成功改判加载成功。

模式列表沿用 PR-05A 的 SDK 候选声明。未知/不支持的参数会报错，硬件限制以实际 Session 为准。
本机示例绑定 IMX219、device 0、mode 2、1920×1080@30；BSP 的模式排序变化后应重新导出。

## 文件保存

SaveTuningProfile 先验证配置，再在目标目录创建独有临时文件；完整写入、文件 fsync、close 后
以 rename 原子替换目标文件，最后 fsync 目录。校验或提交前 I/O 失败不会覆盖原文件，并清理临时文件。
并发读取能读到旧文件或新文件，不会读到半个文件。

返回值分别记录 committed（rename 已完成）和 durable（文件/目录同步成功）。
如果目录同步在 rename 后失败，新文件可能已可见，返回 committed=true/durable=false 并明确报错，
不会声称原文件未变。不会自动创建目的目录；调用方应明确准备好存储位置。

保存离线 Profile 只验证文件结构和参数内在一致性，不假装知道另一台设备的能力。
CLI 保存的是当前实际 Session 的 SDK 快照，加载/应用时再检查实际目标及能力。

## 命令

先停止其他占用相机的 Camera Service/Probe。所有 tuning 命令仍要求 --native，
创建独立 Session，不会通过现有 gRPC 控制后台生产相机。

```bash
cmake --build _output/build/arm64-debug --target camera-ctl \
  native_tuning_profile_hardware_test -j4
mkdir -p _output/runtime/camera-pr05b

# 保存自动参数，也可不指定单项控制，直接保存当前 Session 的 Request 默认值。
_output/build/arm64-debug/bin/camera-ctl tuning save --native \
  --profile _output/runtime/camera-pr05b/auto.yaml --profile-name auto \
  --ae auto --awb auto --report _output/runtime/camera-pr05b/save.json

# 保存经 Metadata 确认的手动参数。
_output/build/arm64-debug/bin/camera-ctl tuning save --native \
  --profile _output/runtime/camera-pr05b/manual.yaml --profile-name manual \
  --exposure-us 8000 --gain 2 --wb-gains 1.5,1,1,1.25

# 新 Session 加载并验证。文件不会被 load 改写。
_output/build/arm64-debug/bin/camera-ctl tuning load --native \
  --profile _output/runtime/camera-pr05b/manual.yaml \
  --report _output/runtime/camera-pr05b/load.json

_output/build/arm64-debug/bin/camera-ctl tuning load --native \
  --profile configs/camera/imx219_tuning.yaml

# Reset 恢复本 Session 创建时的 SDK 默认参数，不删除 Profile。
_output/build/arm64-debug/bin/camera-ctl tuning reset --native
```

load 不接受单项参数覆盖。非默认格式需要同时显式指定匹配的 --device/--width/--height/--fps，
CLI 不根据文件静默切换设备。新 Session 恢复依赖显式 load，不自动加载上次保存的文件。
--report 保存 JSON，包含设备描述、存储提交/同步结果、回滚结果和逐帧 Metadata。
报告路径不能与 Profile 指向同一文件（包括文件别名），防止报告覆盖配置。

## 验证入口

```bash
ctest --test-dir _output/build/arm64-debug \
  -R '^(tuning_profile|camera_tuning|camera_tuning_stub|camera_tuning_cli)_test$' \
  --output-on-failure

_output/build/arm64-debug/bin/native_tuning_profile_hardware_test \
  --output _output/runtime/camera-pr05b/profile-hardware.json

# 包含已有正常生命周期和超时恢复；不注入 SIGKILL。
python3 scripts/tests/jetson-camera-v2-gate.py --normal-only --profiles \
  --continuous-frames 300 --cycles 10 \
  --output-dir _output/runtime/camera-pr05b/gate
```

硬件 Profile 用例默认跑 10 个 Session，每轮验证合法加载、Metadata、7 类非法配置拒绝后旧参数
继续有效、非法保存不覆盖旧文件、Reset 和重新加载；只有显式调用才会打开物理相机。
SDK 拒绝提交或 Metadata 观测失败后的恢复代码保留错误反馈，本轮不通过破坏 daemon 或驱动来注入此类故障。

## 2026-10-26 实测验收

环境：R36.5.2 / IMX219 / device 0 / mode 2 / 1920×1080@30。

| 检查 | 结果 |
| --- | --- |
| 全量 Debug 构建 | 通过 |
| 全量 CTest | 106/106，通过，包含 Profile I/O/并发读写、CLI 和 SDK-disabled stub |
| Profile 真机循环 | 10 个 Session，保存、跨 Session 加载、Reset、重新加载通过 |
| 非法配置拒绝 | 70 次，版本/设备/Model/Mode/尺寸/增益错误均未改写旧有效参数和 revision |
| 帧生命周期 | Acquire/Return/Release 均为 1080，Surface 分配/释放均为 60 |
| Metadata | 手动曝光 8,000,000ns、Sensor Gain=2、ISP Gain=1、WB=[1.5,1,1,1.25] |
| CLI | 最终版 save/load 与仓库自动模式示例均通过；报告覆盖 Profile 的别名检查通过 |
| 既有正常路径 | Diagnostics、RAW 重启、Native 生命周期、连续采集、超时及恢复通过 |
| daemon | Gate 和最终 CLI 验证后 InvocationID 保持一致 |

见 [总体验收](verification/cockpit/camera-pr05b/camera_pr05b_acceptance.json)、
[Profile 循环](verification/cockpit/camera-pr05b/native-tuning-profiles.json)、
[1080 帧 Metadata](verification/cockpit/camera-pr05b/profile-frames.tsv) 和
[最终 CLI 加载](verification/cockpit/camera-pr05b/final-load.json)。

## 后续边界

PR-06 再将 Native 后端接入 CameraService/共享内存/Qt，并在实际采集 Session 上提供控制 RPC。
本轮不修改现有 Proto、生产默认后端、独立 Software ISP 或 Frame Broker/VIC 算法。
Native SIGKILL/daemon 恢复仍是独立生产切换阻塞项，历史失败证据保持原样。
