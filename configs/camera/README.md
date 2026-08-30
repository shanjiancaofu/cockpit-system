# Camera Calibration 配置

本目录用于保存 cockpit-system 自己管理的相机内参和畸变参数。标定文件不放入 ROS2 package；后续
ROS2 bridge 只能加载 `cockpit::hawkeye::CameraCalibration`，再转换为
`sensor_msgs/CameraInfo`。

当前仓库没有经过棋盘格流程验证的 IMX219 参数，因此本目录不提供任何看起来像真实结果的 YAML。

## Guided Calibration V1 状态

软件侧 V1 已完成：粗标定、`solvePnP` 姿态估计、空间/倾斜/距离引导、候选池、Greedy
Farthest Point 关键帧选择、MAD 异常检测、最多 6 轮且最多删除 20% 样本的有限重标定，以及带明确
失败原因的综合 validator。真实采集会输出单一 `NEXT` 操作提示，并在 coverage 满足或达到硬上限时结束。

该状态只表示软件链路完成；实体 Q12-70-5 尚未完成 IMX219 真机验收，所有生成结果仍为
`UNVERIFIED`，不能进入 production CameraInfo。

## 已支持的标定板 profile

`camera-calibrator` 支持显式的 `q12-70-5` profile：

```text
--board-profile q12-70-5
```

该 profile 对应 DAFAN VISION Q12-70-5：12×9 个棋盘格、11×8 个内角点、5.0 mm 方格，
即 `square_size=0.005` m。profile 同时使用适配该小板的 `area_min=0.0005`、
`near_distance=0.25 m`、`far_distance=0.55 m` 和 `tilt_threshold=12°`，避免 FAR 引导与面积
门限互相阻塞。profile 只描述几何参数和采集门限，不代表已经完成 IMX219 真机标定；真实参数在
实体板采集和误差验收前仍保持 `NOT VERIFIED`。

## Jetson 真机 gate

标定板到货后，在 Jetson 原生运行：

```bash
scripts/tests/jetson-camera-calibration-gate.sh
```

需要现场交互调整棋盘位置时，可直接运行带预览的 CLI：

```bash
_output/build/arm64-debug/bin/camera-calibrator \
  --device nvargus://0 --width 1920 --height 1080 --fps 30 \
  --frames 30 --timeout-seconds 300 \
  --board-profile q12-70-5 --preview \
  --output-dir _output/runtime/camera-calibration/imx219-q12-70-5
```

终端会输出中文 `下一步` 指令；预览窗口会持续刷新当前画面，并叠加已接受角点、候选数和空间
coverage。窗口中的状态文字使用 OpenCV ASCII 字体，详细中文动作以终端为准；按 `q` 或 `Esc`
可关闭预览窗口。每收集一个有效候选后，按照终端提示改变棋盘的位置、倾角或距离。

该 gate 固定使用 `nvargus://0`、1920×1080@30 和 `q12-70-5`，输出只写入 `_output/`。
它不会自动修改配置、提交 YAML 或将结果标记为 `VERIFIED`；完成后需要人工检查样本分布、
RMS、逐视角重投影误差和 undistort 对比。

2026-08-30 首次 Jetson gate 已确认 Argus 以 1920×1080@30 启动但未检测到有效棋盘
（`candidates=0`）；第二次运行已检测到 5 个候选，但在 180 秒内未达到采集目标。该结果仅证明
设备和部分棋盘检测链路可用，不证明标定失败或成功。继续采集时按终端 `NEXT` 提示改变棋盘位置、
倾角和距离，并确认对焦/曝光正常、照明均匀且没有被反光或裁切遮挡。

离线或真机运行都会输出 `calibration_result.yaml`、`calibration_report.json`、
`per_view_errors.csv`、`original_preview.jpg` 和 `undistorted_preview.jpg`。报告会记录每个样本的
归一化中心、yaw/pitch/roll、距离、reprojection error、selected/outlier 标记，以及整体 median、
MAD、P95 和空间/姿态/距离 coverage；所有离线结果的 verification 状态固定为 `UNVERIFIED`。

## 文件命名

建议使用：

```text
<camera-id>_<width>x<height>_<calibration-revision>.yaml
```

同一摄像头在不同分辨率、裁剪模式或安装镜头下必须使用不同文件。文件名中的 camera ID 应来自设备
资产配置，不使用 `/dev/videoN` 这种启动后可能变化的路径。

## 字段格式

正式 production YAML 是单层 map，以下字段全部必填，未知字段会被拒绝：

```yaml
image_width: <positive integer>
image_height: <positive integer>
fx: <positive finite number>
fy: <positive finite number>
cx: <finite number>
cy: <finite number>
distortion_model: plumb_bob
k1: <finite number>
k2: <finite number>
p1: <finite number>
p2: <finite number>
k3: <finite number>
```

第一阶段只支持 OpenCV/ROS 常用的五参数 `plumb_bob` 模型。`rational_polynomial` 需要额外的
`k4/k5/k6`，不属于当前数据模型，因此会明确失败。

## 真实标定交付要求

后续真实标定文件至少应同时记录在变更文档中：摄像头序列号、镜头、分辨率、采集模式、标定板规格、
采集日期、标定工具版本、重投影误差和文件 SHA-256。只有完成目标设备上的复测后，才能把状态从
`NOT VERIFIED` 改为已验证。

`calibration_result.yaml` 仅是工具输出，允许携带标定误差和其他诊断字段，始终保持
`UNVERIFIED`；通过真实验收后，才将其中的 production 字段提取到 `configs/camera/<camera>.yaml`。
