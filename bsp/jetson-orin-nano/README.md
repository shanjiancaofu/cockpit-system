# BSP-01 — Jetson Orin Nano / IMX219

独立 Linux Sensor BSP 工程，不重写 `cockpit/driver/camera`，不改 Tegra VI/NVCSI 核心，
不替换 NVIDIA Tegracam 为上游通用 IMX219 驱动。仓库保存完整的 IMX219 驱动、模式表、功能扩展与对应补丁、Overlay 差异及脚本，
便于直接阅读和维护；不复制整份 Linux/NVIDIA BSP，也不提交构建产物。

**当前状态：真实 `.ko` / `.dtbo` 已构建，ABI/CRC 与离线 DT 合并通过；尚未部署/加载新模块。
Test Pattern 真机图像、重启后拓扑、RAW/Argus/视频回归均未验收，不得当作已交付真机 BSP。**
详细记录见 [验收记录](verification.md)。

## 1. 固定目标与来源

`baseline.lock.json` 是机器可读的源版本/文件 SHA-256 锁。目标机实测：

| 项目 | 固定值 |
| --- | --- |
| Jetson Linux | R36.5.2 / OOT kernel variant |
| Kernel | `5.15.199-tegra` / ARM64 / SMP PREEMPT / CONFIG_MODVERSIONS |
| Carrier / module | P3768-0000 + P3767-0005 Super |
| Board compatible | `nvidia,p3768-0000+p3767-0005-super` |
| Camera module | live DT badge `jakku_front_RBP194`，compatible `sony,imx219`；实物型号仍需现场核对 |
| Connector | NVIDIA IMX219-A / 22-pin CSI A，vendor 注释称 cam1 |
| I2C | mux channel 0，sensor `0x10`，当前绑定 `9-0010` |
| CSI / VI | `serial_b`，port-index 1，2 lanes，lane_polarity 6 |
| MCLK / pixel clock / line length | 24 MHz / 182.4 MHz / 3448 |
| RAW 模式 | mode2 1920×1080@30；mode4 1280×720@60；其他模式原样保留 |
| NVIDIA OOT tag/commit | `jetson_36.5.2` / `4adf488f7eba09f84ca2c50ab2ad5aedfdca37f3` |
| NVIDIA DTS tag/commit | `jetson_36.5.2` / `ed1b0f6b113bb050c8cda1ccb411a163f8e2799f` |

**A / cam1 名称不等于 CSI 索引 0。** 本配置仅适用于上述实测开发板与连接；不能照搬到自定义
载板、另一根接口或 IMX219-C。其他拓扑须新增审核过的锁文件与 Overlay，脚本会拒绝不匹配目标。

参考（主来源，不从论坛复制寄存器或设备树）：

- NVIDIA R36.5.2 Sensor Software Driver Programming：
  <https://docs.nvidia.com/jetson/archives/r36.5.2/DeveloperGuide/SD/CameraDevelopment/SensorSoftwareDriverProgramming.html>
- NVIDIA R36.5.2 Working With Sources / Kernel Customization：
  <https://docs.nvidia.com/jetson/archives/r36.5.2/DeveloperGuide/SD/WorkingWithSources.html>
- NVIDIA OOT 源码：<https://gitlab.com/nvidia/nv-tegra/linux-nv-oot>
- NVIDIA DTS 源码：<https://gitlab.com/nvidia/nv-tegra/device/hardware/nvidia/t23x-public-dts>
- Linux IMX219 寄存器/标准菜单对照（**只核对寄存器，不替换 Tegra 驱动**）：
  <https://github.com/torvalds/linux/blob/v5.15/drivers/media/i2c/imx219.c>

## 2. 维护内容与边界

```
bsp/jetson-orin-nano/
├── baseline.lock.json
├── drivers/media/i2c/imx219/
│   ├── nv_imx219.c                     # 完整驱动（官方基线 + 本轮修改）
│   ├── imx219_mode_tbls.h               # 完整模式寄存器表（官方原样）
│   ├── cockpit_imx219_test_pattern.h    # Test Pattern 实现
│   ├── Kbuild
│   └── README.md                       # 驱动阅读顺序
├── device-tree/
│   ├── cockpit-imx219-a.dtsi
│   └── overlays/tegra234-cockpit-imx219-a.dts
├── patches/0001-imx219-test-pattern-lifecycle.patch
├── scripts/{preflight,build,deploy,rollback,verify}.sh
├── scripts/{bsp,deployment,verify}.py
├── tests/{test_bsp.py,test_pattern_test.c}
├── verification.md
└── README.md
```

### 完整源码与补丁的关系

先看 `drivers/media/i2c/imx219/nv_imx219.c`，它是完整 C 驱动，不必先打补丁才能阅读。
模式表和本轮 Test Pattern 实现也在同一个目录。`.c` 中保留 NVIDIA 的原始版权与 GPL 标识。

`patches/` 仍保留，目的是一眼审查“相对官方修改了什么”，不是把补丁当成唯一源码。
构建脚本先在 `_output/` 对固定官方源码打补丁，再逐字节检查该结果与仓库完整 `.c` 一致，
同时检查模式表仍与固定官方版本一致，之后**编译仓库这份完整源码的 staging 副本**。
二者不一致会停止构建，不会偷偷选择某一份。

后续修改驱动的顺序：

1. 编辑完整 `nv_imx219.c`（Test Pattern 的局部实现编辑同目录扩展头）。
2. 如果修改 `.c`，显式刷新差异补丁：

   ```bash
   python3 bsp/jetson-orin-nano/scripts/update-patch.py \
     --oot _output/bsp/r36.5.2/nvidia-oot
   ```

3. 审阅 diff，再执行 `build.sh` 和测试；模式表本轮有意保持官方原样，若要改模式，应另加
   对应模式补丁/验证，不绕过一致性检查。

### Sensor 驱动

- 保留 NVIDIA 的电源时序、Tegracam 接口、增益/曝光/帧率单位和模式表。
- 新增标准 `V4L2_CID_TEST_PATTERN` 与四个 10-bit 颜色分量控件，挂在现有 subdev handler；
  VI 首次打开时照原实现导入控件。使用 subdev registered hook，不改框架。
- 菜单值 **0 Disabled、1 Color Bars、2 Solid Color、3 Grey Color Bars、4 PN9**，Sensor
  `0x0600` 实际写入 **0、2、1、3、4**。16-bit 寄存器通过现有 8-bit regmap 高字节在前写入。
- 设置测试窗口尺寸 `0x0624/0x0626` 与模式对应，颜色寄存器 `0x0602/04/06/08`。
  关电时只缓存；set_mode/STREAMON 在 vendor 软件复位后重新应用，默认 Disabled。
- 用 Sensor 私有 mutex 串行化 power、set_mode、stream、gain/exposure/framerate 和测试图操作。
  模式越界、电源关闭和失败后未复位均明确返回错误。失败不宣称 streaming 成功；I2C 部分写入
  后要求 mode reset/power cycle，不冒充能恢复所有硬件故障。
- 修正 vendor gain 写失败仍返回 0、read_reg 失败仍更新输出、Chip ID 不匹配仍继续等路径。
  从真实模式寄存器初始化 frame_length；拒绝非法因子，避免短曝光减 fine integration 后下溢。
- Probe/Remove 沿用 NVIDIA 注册/注销，加入失败信息与 standby/power-off。没有改 VI/NVCSI/Argus。
  这不意味着 NVIDIA framework 内部所有失败路径均已审计或重构。

### Overlay

通过 `#include` 锁定版本的完整 NVIDIA IMX219-A overlay，然后只维护本工程的 marker 和关键
模式连接差异。DTBO 自含完整 A 拓扑，不要求先叠加第二份 stock A Overlay。构建时验证离线
`base.dtb + 本 DTBO`、模式、传感器→CSI→VI 双向 phandle 连接；**离线通过不等于启动已应用**。
部署替换现有 extlinux 正在引用的 stock Overlay **文件内容**，不修改引用路径，不叠加重复拓扑。

## 3. 只读 Preflight

在仓库根目录运行，普通用户即可：

```bash
bsp/jetson-orin-nano/scripts/preflight.sh
```

报告含真实内核、模块、V4L2 Controls/模式、Media Graph、Argus 进程状态、Secure Boot 和
extlinux。没有权限读内核日志时保留错误，不写“日志正常”。默认输出在
`_output/bsp/r36.5.2/bsp01/preflight/`。

本工程不自动停 daemon、加载模块、写 I2C 寄存器、刷 DTB 或重启。

## 4. 源码准备（显式联网阶段，可替换为离线拷贝）

需要本机匹配的 `nvidia-l4t-kernel-headers`、`nvidia-l4t-kernel-oot-headers`（包括各自
`Module.symvers`）、gcc/make、git、Python 3、dtc/fdtoverlay/fdtget、v4l2-ctl/media-ctl、kmod、
initramfs-tools；版本必须与目标机锁定基线一致，**不要仅为编译而升级运行内核**。

```bash
mkdir -p _output/bsp/r36.5.2
# 仅当目标目录不存在时执行 clone。
git clone --depth 1 --branch jetson_36.5.2 \
  https://gitlab.com/nvidia/nv-tegra/linux-nv-oot.git \
  _output/bsp/r36.5.2/nvidia-oot
git clone --depth 1 --branch jetson_36.5.2 \
  https://gitlab.com/nvidia/nv-tegra/device/hardware/nvidia/t23x-public-dts.git \
  _output/bsp/r36.5.2/t23x-public-dts
```

`build.sh` **不会下载源码**，会验证 commit、受维护原文件 SHA-256 和 tracked worktree；
不对下载树原地打补丁；本地完整驱动只复制到构建 staging。用官方完整 public_sources 源树也可，但必须能验证相同 git commit。
不提供“猜测版本继续编译”选项。

## 5. 构建模块与 DTBO

```bash
bsp/jetson-orin-nano/scripts/build.sh \
  --oot _output/bsp/r36.5.2/nvidia-oot \
  --dts _output/bsp/r36.5.2/t23x-public-dts
```

采用官方外部模块 Kbuild（`make -C <匹配 kernel build> M=<stage> modules`）、NVIDIA 官方
conftest 脚本和已安装的同版本 OOT headers/符号，不修改内核目录、不构建替代核心模块。
conftest 必须探测到目标 I2C probe/remove ABI，不能通过硬编码宏或忽略函数类型错误绕过。
完整内核/其他 OOT 模块构建仍使用 NVIDIA 官方源码构建流程，本仓库不复制/接管整个内核。

产物（默认目录）：

```
_output/bsp/r36.5.2/bsp01/nv_imx219.ko
_output/bsp/r36.5.2/bsp01/tegra234-cockpit-imx219-a.dtbo
_output/bsp/r36.5.2/bsp01/manifest.json
_output/bsp/r36.5.2/bsp01/merged.dtb        # 仅离线校验，不部署它
```

manifest 保存源码/patch/header/artifact 哈希、编译器、配置、kernel/OOT 符号版本、47 个导入
符号 CRC（数量随实际补丁改变）、vermagic、base DTB/stock overlay 摘要与离线端点验证结果。
失败构建会移除旧 manifest，不把残留 `.ko` 当作有效 bundle。

Kernel 与 OOT 符号表中可能有同名但此模块不导入的不同实现（如 host1x）；只对 `.ko` 的实际
导入符号检查 CRC 冲突，不因无关模块符号而误判，也不忽略真正导入项的不匹配。

**该构建没有设备信任的私钥，`.ko` 未签名。** Secure Boot/lockdown 开启或状态未知时部署
脚本拒绝；不禁用安全策略、不伪造签名。关闭强制验签的开发机加载未签名模块可能 taint 内核，
必须在开发验收记录中保留。启用安全启动的产品部署需走受信任的 NVIDIA 签名/密钥流程，
不在本自动部署脚本覆盖范围。

## 6. 部署计划、明确写入与回滚

### 先检查计划（不改系统）

```bash
bsp/jetson-orin-nano/scripts/deploy.sh
```

只支持本机锁定配置，检查 `/lib/modules/.../updates/drivers/media/i2c/nv_imx219.ko` 的实际
解析路径、匹配 ABI、所有已导入符号 CRC、headers/config/symvers/base DTB 摘要、Overlay
离线合并和 **extlinux 的 DEFAULT stanza**。若 initrd 内有旧 `nv_imx219.ko`、Secure Boot
或 lockdown 开启/未知、DKMS/另一份模块覆盖、stock overlay 被修改，拒绝自动安装。

### 有权限的维护窗口才 apply

确认不在采集、录像、RTSP/WebRTC 测试中，先通过现有 camera-ctl 停止采集并退出所有消费者。
然后由操作员明确执行，**命令会改变系统驱动与启动 Overlay**：

```bash
sudo systemctl stop nvargus-daemon
# --apply 不会自动停服务/在线卸载模块/重启。
sudo bsp/jetson-orin-nano/scripts/deploy.sh --apply
```

脚本先检查所有 `/dev/video*`、`/dev/v4l-subdev*`、`/dev/media*` 的占用，创建 root-only 的
`/var/backups/cockpit-bsp01/<时间戳>/`，完整备份原 `.ko`、stock DTBO 和 extlinux.conf。
哈希与安装状态记录于 `backup.json`；fsync/sync 后替换两个文件，depmod 并检查解析结果。
中途软件错误会尝试恢复原文件；若恢复本身失败，明确报告 `MANUAL RECOVERY REQUIRED`。

- 不改 `/boot/Image`、base DTB、initrd 和 extlinux 引用路径。
- 不在线 rmmod/modprobe 新模块，旧已加载模块直到重启仍是旧版本。
- 每个文件的替换是原子的，但**两个文件不是可抗断电的 A/B 事务**。掉电、SIGKILL、磁盘故障
  可能发生在中间；需要备份与维护终端，不能宣称任何失败都自动无损。
- apply 成功后输出准确 backup 路径。保存该路径并准备串口/外部维护启动手段，然后手动：

```bash
sudo reboot
```

### 回滚

若新模块不能加载、Probe/Overlay/采集失败，停止相关服务后：

```bash
sudo systemctl stop nvargus-daemon
# 替换成 deploy 实际输出的备份目录。默认先检查，不写入。
sudo bsp/jetson-orin-nano/scripts/rollback.sh \
  --backup /var/backups/cockpit-bsp01/ACTUAL_TIMESTAMP
sudo bsp/jetson-orin-nano/scripts/rollback.sh \
  --backup /var/backups/cockpit-bsp01/ACTUAL_TIMESTAMP --apply
sudo reboot
```

rollback 不依赖 Sensor Probe 成功或新模块已加载。它核对 root-only 备份、固定允许目标、
备份哈希和当前目标版本；检测到第三方更新时拒绝覆盖，不覆盖后续 apt/管理员修改。
没有递归删除系统目录或自动改变 boot 默认项。若系统无法正常启动，通过串口/恢复介质挂载
原根文件系统，用上述备份恢复模块、DTBO 和 extlinux，再为该根文件系统执行匹配的 depmod。
此灾难恢复需要操作员，不伪装为在线自动回退。

## 7. 重启后的分阶段验证

### A. 只读确认新驱动确实已加载

```bash
bsp/jetson-orin-nano/scripts/verify.sh
```

必须同时有 `/sys/module/nv_imx219/version = bsp01-r36.5.2-1`、live DT 的
`cockpit,bsp-id = imx219-a-r36.5.2-v1`、`/dev/video0` 上可枚举的标准 Test Pattern 控件。
`modinfo` 只看磁盘，不能替代已加载版本；默认检查通过也不等于 RAW/Argus 已通过。
verify JSON 明确标记 scope。未部署时会返回非零并保存失败证据，这是预期结果。

### B. V4L2 RAW/Test Pattern（Argus 必须停止，顺序测试）

```bash
sudo systemctl stop nvargus-daemon
sudo bsp/jetson-orin-nano/scripts/verify.sh --capture \
  --stress "$PWD/_output/build/arm64-debug-auto/bin/camera-stress" \
  --out "$PWD/_output/bsp/r36.5.2/raw-verify"
```

使用现有 v4l2-ctl、可选 camera-stress，不新建用户态采集框架。顺序验证 mode4 720P60 与
mode2 1080P30；设置/读取增益、曝光、帧率及菜单，采集 Solid Black、Solid White、Color Bars
与 Disabled。按实际 RG10 bytesperline 解析 RAW，检查白黑像素变化和彩条同 Bayer 行相位的
空间规律，保存 RAW/PGM、控制 readback 与日志；只设置控件返回成功不算图像通过。
PGM 为 Bayer mosaic 辅助预览，不是 ISP RGB。仍需现场查看图案、确认实际内容正确。

失败时尽力设回 `test_pattern=0`，**不会自动重启 daemon 或隐藏设备异常**。
verify 可能保留大量 RAW 数据，提前检查可用空间。这里仅做短时基本功能，不是长稳测试。

### C. Native libargus / Hardware ISP（与 RAW 分开运行）

确认 RAW 命令已退出且图案 Disabled，操作员再启动 Argus：

```bash
sudo systemctl start nvargus-daemon
bsp/jetson-orin-nano/scripts/verify.sh \
  --argus-probe "$PWD/_output/build/arm64-debug-auto/bin/libargus-capture-probe" \
  --out "$PWD/_output/bsp/r36.5.2/argus-verify"
```

复用现有 native probe 验证 NV12 输出与短时启停。AE/AWB 的显式切换、曝光/增益 metadata
验证仍复用现有 Camera tuning CLI/硬件测试，单独保存结果；probe 的亮度或图像变化不能
自动证明每个 AE/AWB 控制都生效。**不同时启动 V4L2 RAW 和 Argus Sensor Session。**

### D. 用户态回归与异常恢复

按已有 Camera V2/PR-10 文档运行 Native Camera→一份 libx264→RTSP/WebRTC/事件 MP4，浏览器
默认 720P。Qt/HMI 保持此前确定的 Out of Scope，不为 BSP 修改它。原有协议/用户态单元测试
可运行，但不能把旧模块的通过记录算成新 `.ko` 加载后的回归。

`SIGKILL` / outstanding buffers 问题仍按 `docs/CameraV2底层验证.md` 的原始失败记录保留。
本脚本不自动做破坏性 SIGKILL。需要操作员在维护窗口单独运行现有
`jetson-camera-v2-gate.py` 的明确故障项，保存 daemon PID/NRestarts、kernel/Argus journal、
core dump 和恢复前后的结果；即时恢复、systemd 自动重启后恢复分别记录。
若仍落在 NVIDIA 闭源 Argus 清理路径，标明诊断边界，不无依据修改 VI/NVCSI 或声称 Sensor
补丁解决了 daemon 崩溃。

## 8. 主机侧测试（不加载内核模块）

```bash
python3 bsp/jetson-orin-nano/tests/test_bsp.py
```

测试内含 C fault-injection harness，直接编译 Sensor-local 实现，mock I2C/V4L2 检查菜单映射、
16-bit 字节序、首错返回、关电缓存、部分失败后的恢复标志和控件注册。Python 检查版本拒绝、
产物篡改、默认启动 stanza、原子写入、symlink 拒绝、daemon 占用、事务失败恢复和 RAW 像素
判定。不需要 root，不写 /boot，不构造“已通过硬件”的结果。

仓库的三个未提交 PR-10 720P 页面/文档改动与本 BSP 目录独立，不要混进同一个提交。
