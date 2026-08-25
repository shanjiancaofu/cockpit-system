# can-simulator

生成项目原型 VehicleState CAN frame。

终端输出：

```bash
export COCKPIT_RUNTIME_DIR="$PWD/_output/runtime"
_output/build/x86_64-debug/bin/can-simulator --backend stdout \
  --config configs/development.yaml
```

发送到 SocketCAN：

```bash
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
_output/build/x86_64-debug/bin/can-simulator --backend socketcan --interface vcan0 \
  --config configs/development.yaml
```

`--fd-payload-size 64 --brs` 会先发送一帧逐字节递增的 64-byte CAN FD/BRS 诊断帧，再发送车辆原型帧。

在已由系统或管理员配置并启用的真实 SocketCAN 接口上运行端到端 smoke 时，跳过脚本内的
`vcan` 创建步骤：

```bash
CAN_INTERFACE=can0 CAN_SKIP_INTERFACE_SETUP=true bash scripts/tests/vcan-smoke.sh
```

该帧格式仅用于联调，不代表正式车辆协议。

## Chassis Controller 协议

`chassis` 模式复用现有 SocketCAN 后端，周期发送双向 `0x200` heartbeat 和正式 `0x101`
物理速度命令，并解析 STM32 的 Motion、Odometry、Heartbeat 和 Fault CAN FD 帧：

```bash
_output/build/x86_64-debug/bin/can-simulator \
  --backend socketcan --interface vcan0 --protocol chassis \
  --linear-mm-s 500 --angular-mrad-s 0 --interval-ms 20 --samples 100 \
  --config configs/development.yaml
```

结束前工具会用下一 sequence 发送 ENABLE=0、目标为零的停止帧。`stdout` 模式输出 heartbeat、
控制帧以及可选开发握手，可用于检查编码；状态接收必须使用 SocketCAN。开发联调时可显式增加
`--development-handshake` 执行 `0x720/0x721` 三步握手，但正式 `0x101` 不依赖该握手。
协议字段以
`chassis-controller/protocol/chassis_canfd.yaml` 为准。
