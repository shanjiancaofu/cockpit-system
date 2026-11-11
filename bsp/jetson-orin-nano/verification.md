# BSP-01 当前验证记录

记录日期：2026-11-11（Asia/Shanghai）。本轮用户态修复基线：`d47d972`，Sensor 驱动基线保持冻结。
本文件按时间保留部署前基线和失败证据；顶部清单与文末 2026-11-11 收尾章节代表当前结论。

## 部署前实机只读基线（历史记录）

- Jetson Linux R36.5.2，OOT kernel，`5.15.199-tegra`，P3768-0000/P3767-0005 Super。
- `nv_imx219` 来自 `/lib/modules/5.15.199-tegra/updates/drivers/media/i2c/nv_imx219.ko`，
  原模块由 NVIDIA 构建密钥签名。现场 Secure Boot 变量为 disabled。
- I2C `9-0010`，live DT `/bus@0/cam_i2cmux/i2c@0/rbpcv2_imx219_a@10`。
- 实测 Media Graph：IMX219 → NVCSI → VI，端点启用；serial_b / port 1 / 2 lanes / polarity 6。
- V4L2 枚举原生 720P60、1080P30 等 5 个 RAW10 模式；**原驱动未枚举 Test Pattern**。
- 原启动 DEFAULT=JetsonIO，使用 Super base DTB + stock `imx219-A.dtbo`。
- 未修改原驱动、启动文件、daemon 或已运行设备；没有 sudo 权限执行新模块部署。

## 已执行的软件/离线验证

| 检查 | 当前结果 | 不能据此推定 |
| --- | --- | --- |
| NVIDIA OOT/DTS 源码锁定 | 两个 jetson_36.5.2 提交与原文件 SHA-256 验证通过 | 不能替代模块运行 |
| Kernel/OOT headers | 使用安装的匹配配置/符号，检查头文件与 pinned 源码一致 | 不支持任意相近版本 |
| Official conftest / Kbuild | 真正生成 ARM64 `nv_imx219.ko` | 不是空壳或假产物，但未加载 |
| vermagic | `5.15.199-tegra SMP preempt mod_unload modversions aarch64` 匹配 | 不等于 Probe 成功 |
| 导入符号 CRC | 实际 47 个导入符号与目标 kernel/OOT symvers 一致 | 不等于硬件所有行为正确 |
| DTBO | cpp + dtc 真正生成，自含 NVIDIA A 拓扑 | 不等于 bootloader 已应用 |
| base DTB + DTBO | fdtoverlay 离线合并，模式/marker/双向 Sensor-CSI-VI graph 检查通过 | 不等于 live DT 已变化 |
| deploy dry run | 真实目标计划通过，没有写入 | 不等于 root apply/rollback 已在真机执行 |
| 未部署时 verify | 正确返回失败：enhanced module not loaded | 不把磁盘产物当作加载证明 |
| Python + C mock 测试 | 19 项主机测试通过，含 I2C fault injection/部署中途失败恢复 | 不是硬件 Test Pattern 验收 |

构建工具有 compiler version-string 警告：kernel headers 记录 GCC 11.4.0，当前为同系列 Ubuntu
11.4.0 修订包；编译未忽略 ABI 错误，I2C conftest、vermagic 与实际导入 CRC 均核验。
`include/generated/compile.h` 还保留 vendor buildbrain 工具链记录，构建报告保存两者，不将
本地开发构建伪装成 NVIDIA 官方签名产物。

## 当前硬件验收清单

- [x] 维护窗口 apply、重启，新 `.ko` 加载并完成 Sensor Probe。
- [x] live DT marker、CSI/VI 拓扑和目标接线配置一致。
- [x] Test Pattern 自动 RAW 像素/Bayer 相位检查通过；720P/1080P PGM 已人工确认。
- [x] 新模块 RAW10 720P60/1080P30 各 120 帧 × 3 cycles，正常 STREAMON/OFF。
- [x] 修复后的 root `verify.sh` 完整重跑，最终 RAW `verify.json` 为 `passed=true`、`capture_passed=true`，无 Camera kernel anomaly。
- [x] 新 BSP 下 AE/AWB 自动/手动、锁定、Reset 与逐帧 Metadata 通过；Profile 十轮及 70 次非法配置拒绝通过。
- [x] RAW 验收后独立重启 Argus，Native libargus NV12 1080P30、120 帧 × 2 cycles 再次通过。
- [x] 新模块下 Chrome 720P、RTSP 慢客户端隔离、连续事件录像及 MP4 播放器复验通过；此前 Firefox headless 通过，本轮 Snap 环境失败未重计；VLC/Qt Out of Scope。
- [x] Camera Session 停止后旧 Peer 清理，启动后 Generation 1→2、新 Peer 解码、原 RTSP 连接恢复。
- [x] 连续两次事件及 Camera 重启后第三次事件均完成，Session 完整性校验 3 文件/0 issue。
- [x] 真机原版回滚、原版 RAW/Argus 验证及 BSP-01 再部署后的加载/Argus 复验。
- [x] 独立 SIGKILL 对照诊断及恢复记录已保存：Native 仍导致 daemon SEGV，自动重启后可恢复；GStreamer 本轮未触发 daemon 重启。
- [x] 默认 Native 入口改用 Argus-owned EGLStream + 独立池复制后，5 次 probe 和真实 camera_driver SIGKILL 均即时恢复，daemon 无新增重启。
- 旧 NVIDIA BufferStream 二进制及 root-only Core 未解析/未修补；新路径不再注册客户端缓冲，成本为一次 surface copy。

**Sensor BSP 基础功能及真实回滚已通过并冻结；正常调参与媒体/Camera 重启范围也已通过。**
本轮按用户要求排除标准 VLC，不将其标成成功。旧 BufferStream SIGKILL 失败保留为历史证据；
新 Native EGLStream + 独立池复制入口已通过 5 次 probe 及真实 Camera 进程 SIGKILL 恢复，
daemon 不重启。没有修改内核或宣称已修补旧闭源 BufferStream；额外复制成本见文末。
历史测试和失败记录保留；最新结论见文末。

## 本地原始证据（不入 git）

```
_output/bsp/r36.5.2/bsp01-build.log
_output/bsp/r36.5.2/bsp01-final-build.log
_output/bsp/r36.5.2/bsp01/manifest.json
_output/bsp/r36.5.2/bsp01/module-build.log
_output/bsp/r36.5.2/bsp01/conftest.log
_output/bsp/r36.5.2/bsp01/preflight/preflight.json
_output/bsp/r36.5.2/bsp01/merged.dtb
_output/bsp/r36.5.2/deploy-dry-run.json
_output/bsp/r36.5.2/postboot-not-deployed.json
_output/bsp/r36.5.2/host-tests.log
_output/bsp/r36.5.2/bsp01-final-raw/verify.json
_output/bsp/r36.5.2/bsp01-final-raw/stress-1280.json
_output/bsp/r36.5.2/bsp01-final-raw/stress-1920.json
_output/bsp/r36.5.2/bsp01-final-raw/{1280x720,1920x1080}-bars.pgm
_output/bsp/r36.5.2/bsp01-final-argus/verify.json
_output/bsp/r36.5.2/bsp01-final-argus/native-argus.json
```

上述“未部署时 verify”证据仅记录部署前拒绝行为，不代表当前模块状态。后续最终验收继续从
README 第 7 节执行，并保留 deploy 已输出的确切 backup 路径用于真机回滚。

Sensor-local C fault-injection harness 另以 ASan/UBSan 执行通过；这不是运行内核/实际 I2C 的插桩测试。


## 完整驱动源码补充

`drivers/media/i2c/imx219/` 现已保存完整 `nv_imx219.c` 与 `imx219_mode_tbls.h`，不再要求读者
从 patch 拼出主驱动。完整源码与 vendor + patch 的结果在构建前逐字节核对，模式表哈希与
官方锁一致；源码树与下载树都不在构建中被原地修改。
驱动中移除了没有使用的 vendor sibling `camera_gpio.h` include，避免本地目录出现断开的
相对 include；这一改动同样包含在补丁内，不改 Sensor 控制行为。
新一轮构建日志：`_output/bsp/r36.5.2/bsp01-complete-source-build.log`。
这段是当时的源码交付结论；后续章节已经记录模块加载和 Test Pattern 分项硬件结果。


## 重启后硬件验收进展（2026-11-10）

- 已加载模块 `/sys/module/nv_imx219/version = bsp01-r36.5.2-1`。
- live DT marker、mode2/mode4 的 serial_b / port1 / 2 lanes / polarity6 均匹配。
- `/dev/video0` 已枚举 Test Pattern 菜单；四个标准颜色分量位于 `/dev/v4l-subdev1`，名称为
  `red_pixel_value`、`green_red_pixel_value`、`blue_pixel_value`、`green_blue_pixel_value`。
  verify 脚本已按实际标准名称和 subdev 位置修正；此前错误名称没有用于硬件写入。
- boot journal 记录 enhanced module 注册成功；未签名开发模块导致 kernel taint，符合 README
  记录，不当作产品签名通过。
- 新 BSP 下首次 Native libargus 1080P30，120 帧 × 2 cycles 通过，正常销毁 Provider，
  `argus_passed=true`。最终 RAW 验收后又独立执行一次同规格 Argus 验证，结果仍通过。
- 首次 RAW/Test Pattern 验证已写出 720P 的 black/white/bars/disabled 原始帧，但预览生成在每个像素上重复执行
  `max(bars)`，形成二次复杂度并被操作员中断；该次不计为通过。脚本现已缓存最大值、使用查找表生成预览、
  输出阶段进度并在中断时写入失败报告，完整 720P/1080P RAW 验收仍需重新执行。

- 完整 RAW 重跑中，720P/1080P 的 black/white/bars/disabled 均通过像素变化与 Bayer 行相位检查；
  随后的旧 `camera-stress` 首轮失败不是 Sensor 随机丢帧：工具请求 1280×720@60，实际打开成
  3280×2464@30，120/120 帧均带 `V4L2_BUF_FLAG_ERROR`，并触发对应 VI `corr_err err_data 512`。
  根因是通用 MMAP 采集类只设置 `bypass_mode`，没有保持 Tegra `override_enable` 和百万倍率
  `frame_rate`，同时 stress 前没有显式钉住 BSP sensor_mode。现已在 stress 前分步设置 mode/format，
  并通过 `VIDIOC_S_EXT_CTRLS` 在同一采集 FD 上设置 int64 sensor_mode/frame_rate（frame_rate 位于 `VIDIOC_S_PARM` 后）。修复后二者已分别以 120 帧 × 3 cycles
  通过：720P 约 60.048 FPS、1080P 约 30.007 FPS，零 buffer error、零 sequence gap。
- 最终 root RAW 验收输出 `_output/bsp/r36.5.2/bsp01-final-raw/verify.json`：
  `verification_scope=raw_test_pattern`、`passed=true`、`hardware_load_verified=true`、
  `capture_passed=true`、`kernel_anomalies=[]`。两种分辨率的 black/white 平均值分别为 0/65535，
  Color Bars 的 Bayer 行相位一致率均为 1.0；控制读回为 mode4 60,000,000 与 mode2 30,000,000，
  测试结束后 `test_pattern=0 (Disabled)`。人工查看两张 PGM，垂直亮暗区域完整且两种分辨率规律一致。
- 随后启动 `nvargus-daemon` 并生成 `_output/bsp/r36.5.2/bsp01-final-argus/verify.json`：
  `passed=true`、`argus_passed=true`。probe 共获取 240 帧，两个 cycle 的 Capture ID gap、重复、
  回退、timestamp error 和 timeout 均为 0；240 个 lease/buffer 全部归还，12/12 allocation、
  EGL mapping 与 CPU mapping 均平衡，结束后 daemon 保持 active。


## 真机回滚与再部署闭环（2026-11-10）

- 已执行增强版 → 原版回滚并重启；原版模块的 NVIDIA 构建签名恢复，磁盘模块 SHA-256 为
  `3bbf5fd8991fef091b8ce0488fe76244466e6e49e5bebeb1a3299e6d64bef2b3`，原 DTBO SHA-256 为
  `2b9325fac4fe5654e87dbd90c9ccb9824ec3660888e1ed19b91ce8ffc20bd446`。
  原版没有 BSP version/DT marker/Test Pattern 控件，Sensor-CSI-VI 拓扑正常。
- 原版 Native Argus 1080P30、120 帧 × 2 cycles 通过，240 帧/lease/buffer 全部归还。
- 原版 RAW 720P60 和 1080P30 各 120 帧 × 3 cycles 全部通过，约 60.048/30.007 FPS，
  零 sequence gap、buffer error、timeout 和 I/O error。
- 已重新部署增强版并重启；`verify.sh` 确认 `hardware_load_verified=true`，加载版本为
  `bsp01-r36.5.2-1`，Test Pattern 为 Disabled。再部署后独立 Argus 复验
  1080P30、120 帧 × 2 cycles 通过，`passed=true`、`argus_passed=true`。
- 本次证明正常备份回滚与再部署路径可用，不代表断电/无法启动等故障场景均已验证。
  不再重复驱动部署、重启或原版测试；后续直接进入不需重启的用户态验收。

证据（不入 git）：

```
_output/bsp/r36.5.2/rollback-stock-argus/native-argus.json
_output/bsp/r36.5.2/rollback-stock-raw/stress-1280.json
_output/bsp/r36.5.2/rollback-stock-raw/stress-1920.json
_output/bsp/r36.5.2/redeploy-final-argus/verify.json
_output/bsp/r36.5.2/redeploy-final-argus/native-argus.json
```


## 增强版 BSP 下 Hardware ISP 调参回归（2026-11-10）

本轮不修改内核、DTBO 或 Camera 架构，不重新部署/重启，不执行 RAW/SIGKILL。使用
`arm64-debug-auto`，先按当前源码重新构建 `camera-ctl`、
`native_camera_tuning_hardware_test`、`native_tuning_profile_hardware_test`，然后顺序独占 Sensor。
未运行包含重复 RAW 测试的完整 Hardware Gate；以下是定向硬件结果，不冒充完整 Gate 通过。

- `camera-ctl tuning verify --native --frames 30 --warmup 30 --cycles 10`：通过。
  十轮共 70 个阶段、2,400 帧逐帧 Metadata，覆盖手动 AE/AWB、恢复自动、EV/50Hz/60Hz、
  AE/AWB 锁定、Reset 与非法增益拒绝。手动阶段每轮匹配 30 帧，实际 Metadata 为曝光
  8,000,000 ns、Sensor Gain 2.0、ISP Gain 1.0、WB [1.5, 1.0, 1.0, 1.25]，
  对应已提交 Session/Revision；2,400 lease/buffer 全部归还，60/60 allocation 平衡。
- 并发控制硬件测试：通过。Acquire 与 SetTuning 并发正常，旧 lease Metadata 不被后续
  revision 改写；持有 lease 时 Stop 延后，释放后正常停止，新 Session 的 revision 从 1 开始。
- Profile 硬件测试：十轮全部通过，覆盖持久化保存、加载、Reset 后重载和非法配置拒绝，
  共 70 次拒绝、1,080 帧 Metadata；lease/buffer 全部归还，60/60 allocation 平衡。
- 加载版本始终为 `bsp01-r36.5.2-1`；调参验收期间 daemon InvocationID/PID/NRestarts 未变化。

证据（不入 git）：

```
_output/runtime/bsp01-tuning/build.log
_output/runtime/bsp01-tuning/environment.json
_output/runtime/bsp01-tuning/verify-current.json
_output/runtime/bsp01-tuning/concurrent.json
_output/runtime/bsp01-tuning/profiles.json
_output/runtime/bsp01-tuning/acceptance.json
```


## 增强版 BSP 下 720P 媒体并行回归（2026-11-10）

使用重新构建的 `pr10` Native Camera/Transfer/Recording 模块，Camera Session 为
1280×720@30，单个 libx264 输出到 Transfer 8556 和独立 Recording 8557。
未启动 Qt/HMI、未改内核/设备树、未重启 daemon/system。

- Chrome 有界面浏览器实测：720P initial/reconnect/refresh 各连续采样 10 秒，解码帧
  分别 24→325、10→311、15→316；各阶段 packetsLost/framesDropped 为 0。
  截图可见实际相机拍摄的室内画面，不只是 ICE Connected。
- 随后 Firefox headless：720P 三阶段分别 6→307、8→309、7→307 个解码帧，
  packetsLost/framesDropped 为 0；该结果只证明实际解码，不冒充有界面 Firefox 人工观看。
- RTSP 接收与 Chrome/Firefox 验证期间并行：GStreamer 经 RTSP/TCP、H264 depay/parse
  收到 2,400 个 buffer 后正常退出。该证据不是标准 VLC 实播。
- Chrome 播放期间触发事件录像：生成 H.264 Constrained Baseline MP4，1280×720@30，
  时长 20.8 秒，gst-discoverer 检查和硬件解码到 EOS 通过。新 BSP 下该文件的人工播放器
  实播仍待确认，不将历史 BSP 的播放器结果挪用到本轮。
- 运行中 Camera 状态记录 2,840 个 encoder packet、11 次主动 IDR/11 次 forced IDR，
  `frames_dropped=0`，但 `source_frames_skipped=3`，因此不声称整个采集链路绝对零跳帧。
  运行中保留的一个 lease 属于在途帧，不以该运行中快照证明停止后的资源计数平衡。
- 测试正常 Stop 并退出所有自建模块/Driver；Argus PID=986、NRestarts=0，InvocationID
  未变化。测试窗无 Camera kernel error；daemon 在初始 mode4 切换期间有 3 条 acquireBuffer
  Timeout，随后持续播放正常，原始日志保留，不宣称日志完全无错误，也未据此改动 VI/NVCSI。
- 本轮未验证 Camera 停止后保持同一浏览器连接、再启动时旧 Peer 清理/新会话恢复，
  也没有执行 SIGKILL；两项与标准 VLC 人工观看继续保留，不能因自动回归通过而关闭。

证据（不入 git）：

```
_output/runtime/bsp01-media/{build,modules-build}.log
_output/runtime/bsp01-media/regression.json
_output/runtime/bsp01-media/{chrome,firefox}.json
_output/runtime/bsp01-media/chrome-{initial,reconnect,refresh}.png
_output/runtime/bsp01-media/{rtsp,camera-status,recording-trigger,recording-stop}.log
_output/runtime/bsp01-media/{mp4-discover-0,mp4-decode-0}.log
_output/runtime/bsp01-media/{kernel-journal,argus-journal}.txt
```

本轮软件回归：BSP 主机测试 19/19、tuning/profile/CLI CTest 4/4 通过。


## 2026-11-11 收尾：正常媒体通过，异常恢复保留已知限制

- 本轮未修改/安装驱动、未重跑 RAW 或回滚，也没有重启系统。
- 正常媒体报告 `_output/runtime/media-recovery-20261111/final-normal/acceptance.json`
  为 passed=true、daemon_stable=true；Chrome、RTSP 慢 TCP 客户端隔离、三个事件 MP4、
  MP4 播放器与 Session 校验均通过。Camera 停止后旧 Peer 清理，新 Session Generation
  1→2，原 RTSP 连接继续产帧。详情见 `docs/CameraV2-PR10-acceptance.md` 第 10 节。
- 随后独立 SIGKILL：Native 组仍复现 6 outstanding buffers 清理后的 daemon SIGSEGV，
  PID 1007→17696，NRestarts 0→1，systemd 自动重启后 follow-up 成功；GStreamer 对照本轮
  即时恢复且 daemon 不再重启。Core 生成但 root-only，具体崩溃栈未解析，不宣称根因已修复。
- 故障结束后再次 Native 720P30、120×2 帧通过，240 lease/buffer 全部归还，资源平衡；
  目前 daemon active。详情见 `docs/CameraV2底层验证.md` 文末。
- 相关 CTest 11/11 通过，Receiver ASan/UBSan 通过。VLC 人工观看本轮排除，
  1080P 浏览器仍暂缓，Qt/HMI 不纳入；不再要求重复刷驱动或重启。


## Native SIGKILL 后续修复（2026-11-11）

保持 BSP 不变，仅调整 NativeArgusCapture 内部输出流与所有权：Argus-owned EGLStream
经同步 copyToNvBuffer 写入原有有界 NvBufSurface 池，FrameLease/调参/Frame Broker/编码及
传输接口不变。5 轮正式 probe 和真实 Camera 子进程 SIGKILL 后立即重建均通过，daemon
PID=17696/NRestarts=1/InvocationID 不变，NRestarts=1 是旧失败历史值。

六项硬件回归（生命周期/并发调参/Profile/AE-AWB/720P/1080P）均通过，软件 CTest 18/18
通过。额外 surface copy 短时均值约 5.087 ms/720P 帧、7.757 ms/1080P 帧，不再宣称原入口
零复制；并未把 SDK 原始 BufferStream 崩溃说成已修补。详情与复现见
`docs/CameraV2-Argus-SIGKILL.md`；原始证据位于 `_output/runtime/argus-kill-fix/`。
