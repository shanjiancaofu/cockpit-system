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

在已由系统或管理员配置并启用的真实 SocketCAN 接口上运行端到端 smoke 时，跳过脚本内的
`vcan` 创建步骤：

```bash
CAN_INTERFACE=can0 CAN_SKIP_INTERFACE_SETUP=true bash scripts/tests/vcan-smoke.sh
```

该帧格式仅用于联调，不代表正式车辆协议。
