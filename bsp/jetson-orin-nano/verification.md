# BSP-01 当前验证记录

记录日期：2026-11-10（香港时间）。产品代码基线：`5f6765c`；BSP 工作区尚未提交。

## 实机只读基线

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

## 尚未执行的硬件验收（必须保留）

- [ ] 有权限维护窗口 apply、重启，确认新 `.ko` 加载与 Sensor Probe。
- [ ] live DT marker、CSI/VI 拓扑和实物接线一致。
- [ ] Test Pattern 可枚举、可设置，RAW 图案实际变化/正确。
- [ ] 新模块 RAW10 720P/1080P 连续采集和正常启停。
- [ ] 新模块曝光/增益/帧率控制，Native libargus NV12 与 AE/AWB metadata 验证。
- [ ] 新模块后 RTSP/WebRTC/事件 MP4 原链路回归；Qt Out of Scope。
- [ ] 真机安装失败/加载失败的回滚演练。
- [ ] SIGKILL/Argus outstanding-buffer 重新定位与独立恢复记录。

**BSP-01 目前是“代码、构建与离线验证已推进；设备部署/硬件验收待执行”，不是完整验收通过。**
没有把原版本曾成功采集的记录归到新驱动，也没有假定修改 Sensor 就修复闭源 daemon。

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
```

新驱动加载前不要执行 `verify.sh --capture` 并将原模块结果记到本验收表。
后续从 README 的第 6、7 节开始执行，保留 root apply 输出的确切 backup 路径。

Sensor-local C fault-injection harness 另以 ASan/UBSan 执行通过；这不是运行内核/实际 I2C 的插桩测试。


## 完整驱动源码补充

`drivers/media/i2c/imx219/` 现已保存完整 `nv_imx219.c` 与 `imx219_mode_tbls.h`，不再要求读者
从 patch 拼出主驱动。完整源码与 vendor + patch 的结果在构建前逐字节核对，模式表哈希与
官方锁一致；源码树与下载树都不在构建中被原地修改。
驱动中移除了没有使用的 vendor sibling `camera_gpio.h` include，避免本地目录出现断开的
相对 include；这一改动同样包含在补丁内，不改 Sensor 控制行为。
新一轮构建日志：`_output/bsp/r36.5.2/bsp01-complete-source-build.log`。
这仅完善源码交付组织，不代表模块已加载或 Test Pattern 已完成硬件验收。


## 重启后只读 / Argus 进展（2026-11-10）

- 已加载模块 `/sys/module/nv_imx219/version = bsp01-r36.5.2-1`。
- live DT marker、mode2/mode4 的 serial_b / port1 / 2 lanes / polarity6 均匹配。
- `/dev/video0` 已枚举 Test Pattern 菜单；四个标准颜色分量位于 `/dev/v4l-subdev1`，名称为
  `red_pixel_value`、`green_red_pixel_value`、`blue_pixel_value`、`green_blue_pixel_value`。
  verify 脚本已按实际标准名称和 subdev 位置修正；此前错误名称没有用于硬件写入。
- boot journal 记录 enhanced module 注册成功；未签名开发模块导致 kernel taint，符合 README
  记录，不当作产品签名通过。
- 新 BSP 下 Native libargus 1080P30，120 帧 × 2 cycles 通过，正常销毁 Provider，
  `argus_passed=true`。这不替代待执行的 RAW/Test Pattern 图像验证。
- 首次 RAW/Test Pattern 验证已写出 720P 的 black/white/bars/disabled 原始帧，但预览生成在每个像素上重复执行
  `max(bars)`，形成二次复杂度并被操作员中断；该次不计为通过。脚本现已缓存最大值、使用查找表生成预览、
  输出阶段进度并在中断时写入失败报告，完整 720P/1080P RAW 验收仍需重新执行。

- 完整 RAW 重跑中，720P/1080P 的 black/white/bars/disabled 均通过像素变化与 Bayer 行相位检查；
  随后的旧 `camera-stress` 首轮失败不是 Sensor 随机丢帧：工具请求 1280×720@60，实际打开成
  3280×2464@30，120/120 帧均带 `V4L2_BUF_FLAG_ERROR`，并触发对应 VI `corr_err err_data 512`。
  根因是通用 MMAP 采集类只设置 `bypass_mode`，没有保持 Tegra `override_enable` 和百万倍率
  `frame_rate`，同时 stress 前没有显式钉住 BSP sensor_mode。现已在 stress 前分步设置 mode/format，
  并通过 `VIDIOC_S_EXT_CTRLS` 在同一采集 FD 上设置 int64 sensor_mode/frame_rate（frame_rate 位于 `VIDIOC_S_PARM` 后）。修复后二者已分别以 120 帧 × 3 cycles
  通过：720P 约 60.048 FPS、1080P 约 30.007 FPS，零 buffer error、零 sequence gap。仍需通过
  root verify 完整重跑，生成统一的 kernel-journal/`verify.json` 验收报告。
