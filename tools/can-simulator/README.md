# can-simulator

生成项目原型 VehicleState CAN frame。

终端输出：

```bash
export COCKPIT_RUNTIME_DIR="$PWD/_output/runtime"
_output/build/x86_64-debug/bin/can-simulator --backend stdout --config configs/config.yaml
```

发送到 SocketCAN：

```bash
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
_output/build/x86_64-debug/bin/can-simulator --backend socketcan --interface vcan0 --config configs/config.yaml
```

该帧格式仅用于联调，不代表正式车辆协议。
