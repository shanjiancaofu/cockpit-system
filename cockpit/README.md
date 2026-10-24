# Cockpit V2 产品源码

`cockpit/` 固定为 17 个一级业务模块，各目录自行声明实现 target：

| 模块 | 职责 |
| --- | --- |
| [navigator](navigator/README.md) | 进程管理、模块加载、生命周期 |
| [driver](driver/README.md) | Camera、Audio、CAN、车辆硬件所有权 |
| [voice](voice/README.md) | 智能语音交互 |
| [agent](agent/README.md) | 模型部署、推理 Runtime、多模态与模型调度 |
| [eunomia](eunomia/README.md) | 自动化标定 |
| [hawkeye](hawkeye/README.md) | 视觉感知与监测 |
| [ros](ros/README.md) | ROS2/Nav2 集成、Camera/底盘消息适配 |
| [exchange](exchange/README.md) | Protobuf/gRPC 合同与消息适配 |
| [transfer](transfer/README.md) | 网页与远程传输能力 |
| [carcloud](carcloud/README.md) | 车云业务 |
| [debugger](debugger/README.md) | 日志、故障诊断、调试信息与 Jara 上报 |
| [spider](spider/README.md) | 数据采集、汇聚、预处理和分发 |
| [geois](geois/README.md) | 地图、定位和地理信息 |
| [hmi](hmi/README.md) | Qt/QML 座舱交互 |
| [shield](shield/README.md) | 安全防护、最终执行约束、故障保护 |
| [upgrader](upgrader/README.md) | OTA、A/B 与回滚 |
| [vanguard](vanguard/README.md) | 主动风险判断与预警 |

公共基础能力及接口位于 `exchange/foundation/`；日志实现在 debugger。
不在仓库根目录增加 common，也不保留旧 core/modules/library/apps 或根 agent 源码聚合目录。
17 个目录不是 17 个进程。现有 Navigator ABI、Protobuf、动态库文件名、Unix Socket、mode 和运行配置保持兼容。
详细迁移表和验收见 [V2 架构重组](../docs/V2架构重组.md)。
