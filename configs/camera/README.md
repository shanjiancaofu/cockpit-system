# Camera Calibration 配置

本目录用于保存 cockpit-system 自己管理的相机内参和畸变参数。标定文件不放入 ROS2 package；后续
ROS2 bridge 只能加载 `cockpit::hawkeye::CameraCalibration`，再转换为
`sensor_msgs/CameraInfo`。

当前仓库没有经过棋盘格流程验证的 IMX219 参数，因此本目录不提供任何看起来像真实结果的 YAML。

## 文件命名

建议使用：

```text
<camera-id>_<width>x<height>_<calibration-revision>.yaml
```

同一摄像头在不同分辨率、裁剪模式或安装镜头下必须使用不同文件。文件名中的 camera ID 应来自设备
资产配置，不使用 `/dev/videoN` 这种启动后可能变化的路径。

## 字段格式

标定 YAML 是单层 map，以下字段全部必填，未知字段会被拒绝：

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
