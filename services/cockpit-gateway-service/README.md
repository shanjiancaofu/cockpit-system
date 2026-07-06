# cockpit-gateway-service

面向 UI、topic 和语音动作的本地数据聚合进程。

当前能力：

- 订阅 `vehicle-data-service` 的 VehicleState stream。
- 保存最新车辆状态快照。
- 拒绝超过两秒的陈旧状态。
- 向 UI/topic 发布 CockpitEvent stream。
- 为语音动作提供车辆状态查询。

WebSocket 地址已进入配置，但真实 WebSocket server 尚未实现。
