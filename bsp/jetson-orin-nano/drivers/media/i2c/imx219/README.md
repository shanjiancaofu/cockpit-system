# 先从这里看完整 IMX219 驱动

这个目录现在有完整实现，不是只有补丁或接口声明。

| 文件 | 是什么 | 先看什么 |
| --- | --- | --- |
| `nv_imx219.c` | 完整 Sensor 驱动：NVIDIA R36.5.2 基线 + 本工程修改 | 先从末尾的 `imx219_i2c_driver` 看入口 |
| `imx219_mode_tbls.h` | 完整 Sensor 模式寄存器表，官方原样 | 1080P30、720P60、COMMON、START/STOP 表 |
| `cockpit_imx219_test_pattern.h` | 本轮新增 Test Pattern 的具体实现 | 菜单值到寄存器值的映射和 I2C 错误返回 |
| `Kbuild` | 让 Linux 内核构建系统生成 `.ko` 的配置 | `obj-m += nv_imx219.o` |

## 按设备工作的顺序读

### 1. 系统识别传感器：Probe

设备树的 `compatible = "sony,imx219"` 匹配 `imx219_of_match`，I2C 驱动调用
`imx219_probe()`，注册 Tegracam、读取 Sensor Chip ID、注册 V4L2 subdev 和控件。
这一步不是采集视频，只是识别并把传感器接入内核 Camera 框架。

### 2. 上电和准备模式

- `imx219_power_get()`：取得 GPIO、时钟、电源等资源。
- `imx219_power_on()`：执行上电顺序并维护状态。
- `imx219_set_mode()`：写 COMMON 与指定模式寄存器表、读取 frame length、恢复测试图设置。

模式寄存器写在哪里？在 `imx219_mode_tbls.h`，不是由用户态循环写 I2C。

### 3. 调节图像参数

- `imx219_set_gain()`：把框架的增益值换算成 Sensor 寄存器值。
- `imx219_set_exposure()`：根据像素时钟和行长换算曝光行数。
- `imx219_set_frame_rate()`：设置 frame length，控制帧率。

`*_unlocked()` 是这些函数的实际计算/写寄存器部分；外层负责与上电、切模式和 Test Pattern
串行化，避免并发写 Sensor。它们不是另一套驱动。

### 4. 开始、停止输出

- `imx219_start_streaming()`：重新应用测试图设置，再写 Sensor STREAMON 表。
- `imx219_stop_streaming()`：写 STREAMOFF 表，检查错误并维护 streaming 状态。
- Remove 时注销 subdev，尽力待机/断电，释放资源。

这里只负责 Sensor 输出。接收 CSI 数据、Hardware ISP、用户态 libargus 是其他层，并不是
应当全部塞进 `nv_imx219.c` 的内容。

### 5. 本轮新增 Test Pattern 怎么走

```text
v4l2-ctl 设置标准 test_pattern 控件
    ↓
cockpit_imx219_s_ctrl()
    ↓
检查电源/模式/错误状态
    ↓
cockpit_imx219_apply_pattern()
    ↓
imx219_write_reg() / regmap → I2C → Sensor 寄存器
```

菜单 `1 = Color Bars`，实际 Sensor 寄存器值为 `2`，所以需要映射表。
关电时控件只缓存；重新启动模式后应用缓存。这些逻辑在扩展头中实现，主 `.c` 会包含它。
标准控件通过现有 Tegracam subdev handler 注册，不需要另做用户态采集服务。

## 哪些地方不是本项目重新写的

NVIDIA 原有 I2C/Tegracam 接入、电源顺序、模式表和原始控制换算来自固定版本官方驱动，
保留版权与 GPL 标识。本项目新增/修正的内容可以看
`../../../../patches/0001-imx219-test-pattern-lifecycle.patch`，或主目录 README。
完整源码用来阅读和开发，补丁用来审查差异；构建会检查它们一致。

## 构建与部署不要在这个目录直接乱执行

从仓库根目录使用主 README 的 `scripts/build.sh`，产物放在 `_output/`。
有源码、能生成 `.ko`，不等于新模块已经在设备加载；硬件验收状态见主目录 `verification.md`。
